/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/effects/GrXfermodeFragmentProcessor.h"

#include "src/core/SkXfermodePriv.h"
#include "src/gpu/GrFragmentProcessor.h"
#include "src/gpu/SkGr.h"
#include "src/gpu/effects/generated/GrConstColorProcessor.h"
#include "src/gpu/glsl/GrGLSLBlend.h"
#include "src/gpu/glsl/GrGLSLFragmentProcessor.h"
#include "src/gpu/glsl/GrGLSLFragmentShaderBuilder.h"

using GrXfermodeFragmentProcessor::ComposeBehavior;

// Some of the cpu implementations of blend modes differ too much from the GPU enough that
// we can't use the cpu implementation to implement constantOutputForConstantInput.
static inline bool does_cpu_blend_impl_match_gpu(SkBlendMode mode) {
    // The non-seperable modes differ too much. So does SoftLight. ColorBurn differs too much on our
    // test iOS device (but we just disable it across the aboard since it may happen on untested
    // GPUs).
    return mode <= SkBlendMode::kLastSeparableMode && mode != SkBlendMode::kSoftLight &&
           mode != SkBlendMode::kColorBurn;
}

static const char* ComposeBehavior_Name(ComposeBehavior behavior) {
    SkASSERT(unsigned(behavior) <= unsigned(ComposeBehavior::kLastComposeBehavior));
    static constexpr const char* gStrings[] = {
        "Default",
        "Compose-One",
        "Compose-Two",
        "SkMode",
    };
    static_assert(SK_ARRAY_COUNT(gStrings) == size_t(ComposeBehavior::kLastComposeBehavior) + 1);
    return gStrings[int(behavior)];
}

//////////////////////////////////////////////////////////////////////////////

class ComposeFragmentProcessor : public GrFragmentProcessor {
public:
    static std::unique_ptr<GrFragmentProcessor> Make(std::unique_ptr<GrFragmentProcessor> src,
                                                     std::unique_ptr<GrFragmentProcessor> dst,
                                                     SkBlendMode mode, ComposeBehavior behavior) {
        return std::unique_ptr<GrFragmentProcessor>(
                new ComposeFragmentProcessor(std::move(src), std::move(dst), mode, behavior));
    }

    const char* name() const override { return "Compose"; }

#ifdef SK_DEBUG
    SkString dumpInfo() const override {
        SkString str;
        str.appendf("Mode: %s", SkBlendMode_Name(fMode));
        return str;
    }
#endif

    std::unique_ptr<GrFragmentProcessor> clone() const override;

    SkBlendMode getMode() const { return fMode; }
    ComposeBehavior composeBehavior() const { return fComposeBehavior; }

private:
    ComposeFragmentProcessor(std::unique_ptr<GrFragmentProcessor> src,
                             std::unique_ptr<GrFragmentProcessor> dst,
                             SkBlendMode mode, ComposeBehavior behavior)
            : INHERITED(kComposeFragmentProcessor_ClassID, OptFlags(src.get(), dst.get(), mode))
            , fMode(mode)
            , fComposeBehavior(behavior) {
        if (fComposeBehavior == ComposeBehavior::kDefault) {
            fComposeBehavior = (src && dst) ? ComposeBehavior::kComposeTwoBehavior
                                            : ComposeBehavior::kComposeOneBehavior;
        }
        this->registerChild(std::move(src));
        this->registerChild(std::move(dst));
    }

    ComposeFragmentProcessor(const ComposeFragmentProcessor& that)
            : INHERITED(kComposeFragmentProcessor_ClassID, ProcessorOptimizationFlags(&that))
            , fMode(that.fMode)
            , fComposeBehavior(that.fComposeBehavior) {
        this->cloneAndRegisterAllChildProcessors(that);
    }

    static OptimizationFlags OptFlags(const GrFragmentProcessor* src,
                                      const GrFragmentProcessor* dst, SkBlendMode mode) {
        OptimizationFlags flags;
        switch (mode) {
            case SkBlendMode::kClear:
            case SkBlendMode::kSrc:
            case SkBlendMode::kDst:
                SK_ABORT("Shouldn't have created a Compose FP as 'clear', 'src', or 'dst'.");
                flags = kNone_OptimizationFlags;
                break;

            // Produces opaque if both src and dst are opaque. These also will modulate the child's
            // output by either the input color or alpha. However, if the child is not compatible
            // with the coverage as alpha then it may produce a color that is not valid premul.
            case SkBlendMode::kSrcIn:
            case SkBlendMode::kDstIn:
            case SkBlendMode::kModulate:
                if (src && dst) {
                    flags = ProcessorOptimizationFlags(src) & ProcessorOptimizationFlags(dst) &
                            kPreservesOpaqueInput_OptimizationFlag;
                } else if (src) {
                    flags = ProcessorOptimizationFlags(src) &
                            ~kConstantOutputForConstantInput_OptimizationFlag;
                } else if (dst) {
                    flags = ProcessorOptimizationFlags(dst) &
                            ~kConstantOutputForConstantInput_OptimizationFlag;
                } else {
                    flags = kNone_OptimizationFlags;
                }
                break;

            // Produces zero when both are opaque, indeterminate if one is opaque.
            case SkBlendMode::kSrcOut:
            case SkBlendMode::kDstOut:
            case SkBlendMode::kXor:
                flags = kNone_OptimizationFlags;
                break;

            // Is opaque if the dst is opaque.
            case SkBlendMode::kSrcATop:
                flags = (dst ? ProcessorOptimizationFlags(dst) : kAll_OptimizationFlags) &
                         kPreservesOpaqueInput_OptimizationFlag;
                break;

            // DstATop is the converse of kSrcATop. Screen is also opaque if the src is a opaque.
            case SkBlendMode::kDstATop:
            case SkBlendMode::kScreen:
                flags = (src ? ProcessorOptimizationFlags(src) : kAll_OptimizationFlags) &
                         kPreservesOpaqueInput_OptimizationFlag;
                break;

            // These modes are all opaque if either src or dst is opaque. All the advanced modes
            // compute alpha as src-over.
            case SkBlendMode::kSrcOver:
            case SkBlendMode::kDstOver:
            case SkBlendMode::kPlus:
            case SkBlendMode::kOverlay:
            case SkBlendMode::kDarken:
            case SkBlendMode::kLighten:
            case SkBlendMode::kColorDodge:
            case SkBlendMode::kColorBurn:
            case SkBlendMode::kHardLight:
            case SkBlendMode::kSoftLight:
            case SkBlendMode::kDifference:
            case SkBlendMode::kExclusion:
            case SkBlendMode::kMultiply:
            case SkBlendMode::kHue:
            case SkBlendMode::kSaturation:
            case SkBlendMode::kColor:
            case SkBlendMode::kLuminosity:
                flags = ((src ? ProcessorOptimizationFlags(src) : kAll_OptimizationFlags) |
                         (dst ? ProcessorOptimizationFlags(dst) : kAll_OptimizationFlags)) &
                         kPreservesOpaqueInput_OptimizationFlag;
                break;
        }
        if (does_cpu_blend_impl_match_gpu(mode) &&
            (src ? src->hasConstantOutputForConstantInput() : true) &&
            (dst ? dst->hasConstantOutputForConstantInput() : true)) {
            flags |= kConstantOutputForConstantInput_OptimizationFlag;
        }
        return flags;
    }

    void onGetGLSLProcessorKey(const GrShaderCaps& caps, GrProcessorKeyBuilder* b) const override {
        b->add32((int)fMode);
    }

    bool onIsEqual(const GrFragmentProcessor& other) const override {
        const ComposeFragmentProcessor& cs = other.cast<ComposeFragmentProcessor>();
        return fMode == cs.fMode;
    }

    SkPMColor4f constantOutputForConstantInput(const SkPMColor4f& input) const override {
        const auto* src = this->childProcessor(0);
        const auto* dst = this->childProcessor(1);

        switch (fComposeBehavior) {
            case ComposeBehavior::kComposeOneBehavior: {
                SkPMColor4f srcColor = src ? ConstantOutputForConstantInput(src, SK_PMColor4fWHITE)
                                           : input;
                SkPMColor4f dstColor = dst ? ConstantOutputForConstantInput(dst, SK_PMColor4fWHITE)
                                           : input;
                return SkBlendMode_Apply(fMode, srcColor, dstColor);
            }

            case ComposeBehavior::kComposeTwoBehavior: {
                SkPMColor4f opaqueInput = { input.fR, input.fG, input.fB, 1 };
                SkPMColor4f srcColor = ConstantOutputForConstantInput(src, opaqueInput);
                SkPMColor4f dstColor = ConstantOutputForConstantInput(dst, opaqueInput);
                SkPMColor4f result = SkBlendMode_Apply(fMode, srcColor, dstColor);
                return result * input.fA;
            }

            case ComposeBehavior::kSkModeBehavior: {
                SkPMColor4f srcColor = src ? ConstantOutputForConstantInput(src, SK_PMColor4fWHITE)
                                           : input;
                SkPMColor4f dstColor = dst ? ConstantOutputForConstantInput(dst, input)
                                           : input;
                return SkBlendMode_Apply(fMode, srcColor, dstColor);
            }

            default:
                SK_ABORT("unrecognized compose behavior");
                return input;
        }
    }

    GrGLSLFragmentProcessor* onCreateGLSLInstance() const override;

    SkBlendMode fMode;
    ComposeBehavior fComposeBehavior;

    GR_DECLARE_FRAGMENT_PROCESSOR_TEST

    typedef GrFragmentProcessor INHERITED;
};

/////////////////////////////////////////////////////////////////////

class GLComposeFragmentProcessor : public GrGLSLFragmentProcessor {
public:
    void emitCode(EmitArgs&) override;

private:
    typedef GrGLSLFragmentProcessor INHERITED;
};

/////////////////////////////////////////////////////////////////////

GR_DEFINE_FRAGMENT_PROCESSOR_TEST(ComposeFragmentProcessor);

#if GR_TEST_UTILS
std::unique_ptr<GrFragmentProcessor> ComposeFragmentProcessor::TestCreate(GrProcessorTestData* d) {
    // Create two random frag procs.
    std::unique_ptr<GrFragmentProcessor> fpA(GrProcessorUnitTest::MakeChildFP(d));
    std::unique_ptr<GrFragmentProcessor> fpB(GrProcessorUnitTest::MakeChildFP(d));

    SkBlendMode mode;
    ComposeBehavior behavior;
    do {
        mode = static_cast<SkBlendMode>(d->fRandom->nextRangeU(0, (int)SkBlendMode::kLastMode));
        behavior = static_cast<ComposeBehavior>(
                       d->fRandom->nextRangeU(0, (int)ComposeBehavior::kLastComposeBehavior));
    } while (SkBlendMode::kClear == mode || SkBlendMode::kSrc == mode || SkBlendMode::kDst == mode);
    return std::unique_ptr<GrFragmentProcessor>(
            new ComposeFragmentProcessor(std::move(fpA), std::move(fpB), mode, behavior));
}
#endif

std::unique_ptr<GrFragmentProcessor> ComposeFragmentProcessor::clone() const {
    return std::unique_ptr<GrFragmentProcessor>(new ComposeFragmentProcessor(*this));
}

GrGLSLFragmentProcessor* ComposeFragmentProcessor::onCreateGLSLInstance() const{
    return new GLComposeFragmentProcessor;
}

/////////////////////////////////////////////////////////////////////

void GLComposeFragmentProcessor::emitCode(EmitArgs& args) {

    GrGLSLFPFragmentBuilder* fragBuilder = args.fFragBuilder;
    const ComposeFragmentProcessor& cs = args.fFp.cast<ComposeFragmentProcessor>();
    SkBlendMode mode = cs.getMode();
    ComposeBehavior behavior = cs.composeBehavior();

    // Load the input color and make an opaque copy if needed.
    fragBuilder->codeAppendf("// %s Xfer Mode: %s\n",
                             ComposeBehavior_Name(behavior), SkBlendMode_Name(mode));

    SkString srcColor, dstColor;
    switch (behavior) {
        case ComposeBehavior::kComposeOneBehavior:
            // Compose-one operations historically leave the alpha on the input color.
            srcColor = cs.childProcessor(0) ? this->invokeChild(0, "half4(1)", args)
                                            : SkString(args.fInputColor);
            dstColor = cs.childProcessor(1) ? this->invokeChild(1, "half4(1)", args)
                                            : SkString(args.fInputColor);
            break;

        case ComposeBehavior::kComposeTwoBehavior:
            // Compose-two operations historically have forced the input color to opaque.
            fragBuilder->codeAppendf("half4 inputOpaque = %s.rgb1;\n", args.fInputColor);
            srcColor = this->invokeChild(0, "inputOpaque", args);
            dstColor = this->invokeChild(1, "inputOpaque", args);
            break;

        case ComposeBehavior::kSkModeBehavior:
            // SkModeColorFilter operations act like ComposeOne, but pass the input color to dst.
            srcColor = cs.childProcessor(0) ? this->invokeChild(0, "half4(1)", args)
                                            : SkString(args.fInputColor);
            dstColor = cs.childProcessor(1) ? this->invokeChild(1, args.fInputColor, args)
                                            : SkString(args.fInputColor);
            break;

        default:
            SK_ABORT("unrecognized compose behavior");
            break;
    }

    // Blend src and dst colors together.
    GrGLSLBlend::AppendMode(fragBuilder, srcColor.c_str(), dstColor.c_str(),
                            args.fOutputColor, mode);

    // Reapply alpha from input color if we are doing a compose-two.
    if (behavior == ComposeBehavior::kComposeTwoBehavior) {
        fragBuilder->codeAppendf("%s *= %s.a;\n", args.fOutputColor, args.fInputColor);
    }
}

//////////////////////////////////////////////////////////////////////////////

std::unique_ptr<GrFragmentProcessor> GrXfermodeFragmentProcessor::Make(
        std::unique_ptr<GrFragmentProcessor> src,
        std::unique_ptr<GrFragmentProcessor> dst,
        SkBlendMode mode, ComposeBehavior behavior) {
    switch (mode) {
        case SkBlendMode::kClear:
            return GrConstColorProcessor::Make(SK_PMColor4fTRANSPARENT);
        case SkBlendMode::kSrc:
            return GrFragmentProcessor::OverrideInput(std::move(src), SK_PMColor4fWHITE,
                                                      /*useUniform=*/false);
        case SkBlendMode::kDst:
            return GrFragmentProcessor::OverrideInput(std::move(dst), SK_PMColor4fWHITE,
                                                      /*useUniform=*/false);
        default:
            return ComposeFragmentProcessor::Make(std::move(src), std::move(dst), mode, behavior);
    }
}
