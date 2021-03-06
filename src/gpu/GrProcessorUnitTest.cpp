/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/GrProcessorUnitTest.h"

#include "include/gpu/GrRecordingContext.h"
#include "src/gpu/GrFragmentProcessor.h"
#include "src/gpu/GrRecordingContextPriv.h"

#if GR_TEST_UTILS

class GrGeometryProcessor;

GrProcessorTestData::GrProcessorTestData(SkRandom* random, GrRecordingContext* context,
                                         int numViews, const ViewInfo views[])
        : GrProcessorTestData(random, context, numViews, views, /*inputFP=*/nullptr) {}

GrProcessorTestData::GrProcessorTestData(SkRandom* random, GrRecordingContext* context,
                                         int numViews, const ViewInfo views[],
                                         std::unique_ptr<GrFragmentProcessor> inputFP)
        : fRandom(random), fContext(context), fInputFP(std::move(inputFP)) {
    fViews.reset(views, numViews);
    fArena = std::unique_ptr<SkArenaAlloc>(new SkArenaAlloc(1000));
}

GrProcessorTestData::~GrProcessorTestData() {}

GrProxyProvider* GrProcessorTestData::proxyProvider() { return fContext->priv().proxyProvider(); }

const GrCaps* GrProcessorTestData::caps() { return fContext->priv().caps(); }

std::unique_ptr<GrFragmentProcessor> GrProcessorTestData::inputFP() { return std::move(fInputFP); }

GrProcessorTestData::ViewInfo GrProcessorTestData::randomView() {
    SkASSERT(!fViews.empty());
    return fViews[fRandom->nextULessThan(fViews.count())];
}

GrProcessorTestData::ViewInfo GrProcessorTestData::randomAlphaOnlyView() {
    int numAlphaOnly = 0;
    for (const auto& [v, ct, at] : fViews) {
        if (GrColorTypeIsAlphaOnly(ct)) {
            ++numAlphaOnly;
        }
    }
    SkASSERT(numAlphaOnly);
    int idx = fRandom->nextULessThan(numAlphaOnly);
    for (const auto& [v, ct, at] : fViews) {
        if (GrColorTypeIsAlphaOnly(ct) && !idx--) {
            return {v, ct, at};
        }
    }
    SkUNREACHABLE;
}

template <class ProcessorSmartPtr>
GrProcessorTestFactory<ProcessorSmartPtr>::GrProcessorTestFactory(MakeProc makeProc) {
    fMakeProc = makeProc;
    GetFactories()->push_back(this);
}

template <class ProcessorSmartPtr>
ProcessorSmartPtr GrProcessorTestFactory<ProcessorSmartPtr>::Make(GrProcessorTestData* data) {
    VerifyFactoryCount();
    if (GetFactories()->count() == 0) {
        return nullptr;
    }
    uint32_t idx = data->fRandom->nextULessThan(GetFactories()->count());
    return MakeIdx(idx, data);
}

template <class ProcessorSmartPtr>
ProcessorSmartPtr GrProcessorTestFactory<ProcessorSmartPtr>::MakeIdx(int idx,
                                                                     GrProcessorTestData* data) {
    SkASSERT(idx < GetFactories()->count());
    GrProcessorTestFactory<ProcessorSmartPtr>* factory = (*GetFactories())[idx];
    ProcessorSmartPtr processor = factory->fMakeProc(data);
    SkASSERT(processor);
    return processor;
}

template <class ProcessorSmartPtr>
int GrProcessorTestFactory<ProcessorSmartPtr>::Count() {
    return GetFactories()->count();
}

GrXPFactoryTestFactory::GrXPFactoryTestFactory(GetFn* getProc) : fGetProc(getProc) {
    GetFactories()->push_back(this);
}

const GrXPFactory* GrXPFactoryTestFactory::Get(GrProcessorTestData* data) {
    VerifyFactoryCount();
    if (GetFactories()->count() == 0) {
        return nullptr;
    }
    uint32_t idx = data->fRandom->nextRangeU(0, GetFactories()->count() - 1);
    const GrXPFactory* xpf = (*GetFactories())[idx]->fGetProc(data);
    SkASSERT(xpf);
    return xpf;
}

/*
 * Originally these were both in the processor unit test header, but then it seemed to cause linker
 * problems on android.
 */
template <>
SkTArray<GrFragmentProcessorTestFactory*, true>* GrFragmentProcessorTestFactory::GetFactories() {
    static SkTArray<GrFragmentProcessorTestFactory*, true> gFactories;
    return &gFactories;
}

template <>
SkTArray<GrGeometryProcessorTestFactory*, true>* GrGeometryProcessorTestFactory::GetFactories() {
    static SkTArray<GrGeometryProcessorTestFactory*, true> gFactories;
    return &gFactories;
}

SkTArray<GrXPFactoryTestFactory*, true>* GrXPFactoryTestFactory::GetFactories() {
    static SkTArray<GrXPFactoryTestFactory*, true> gFactories;
    return &gFactories;
}

/*
 * To ensure we always have successful static initialization, before creating from the factories
 * we verify the count is as expected.  If a new factory is added, then these numbers must be
 * manually adjusted.
 */
static constexpr int kFPFactoryCount = 37;
static constexpr int kGPFactoryCount = 14;
static constexpr int kXPFactoryCount = 4;

template <> void GrFragmentProcessorTestFactory::VerifyFactoryCount() {
    if (kFPFactoryCount != GetFactories()->count()) {
        SkDebugf("\nExpected %d fragment processor factories, found %d.\n", kFPFactoryCount,
                 GetFactories()->count());
        SK_ABORT("Wrong number of fragment processor factories!");
    }
}

template <> void GrGeometryProcessorTestFactory::VerifyFactoryCount() {
    if (kGPFactoryCount != GetFactories()->count()) {
        SkDebugf("\nExpected %d geometry processor factories, found %d.\n", kGPFactoryCount,
                 GetFactories()->count());
        SK_ABORT("Wrong number of geometry processor factories!");
    }
}

void GrXPFactoryTestFactory::VerifyFactoryCount() {
    if (kXPFactoryCount != GetFactories()->count()) {
        SkDebugf("\nExpected %d xp factory factories, found %d.\n", kXPFactoryCount,
                 GetFactories()->count());
        SK_ABORT("Wrong number of xp factory factories!");
    }
}

std::unique_ptr<GrFragmentProcessor> GrProcessorUnitTest::MakeChildFP(GrProcessorTestData* data) {
    std::unique_ptr<GrFragmentProcessor> fp;
    do {
        fp = GrFragmentProcessorTestFactory::Make(data);
        SkASSERT(fp);
    } while (fp->numNonNullChildProcessors() != 0);
    return fp;
}

template class GrProcessorTestFactory<GrGeometryProcessor*>;
template class GrProcessorTestFactory<std::unique_ptr<GrFragmentProcessor>>;

#endif
