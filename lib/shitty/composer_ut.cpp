/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "options.h"
#include "composer.h"
#include "input_bindings.h"

#include <lib/vterm/listener.h>
#include <lib/vterm/cell_extra_store.h>

#include <std/tst/ut.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct StateListener final: public Listener {
        explicit StateListener(Composer& composer);

        void onListen(void* argument) override;

        Composer& composer;
        CellExtraStore* extras = nullptr;
        u16 columns = 0;
        u16 rows = 0;
        u16 pixelWidth = 0;
        u16 pixelHeight = 0;
        float contentScale = 0.0f;
        size_t calls = 0;
        bool argumentWasNull = false;
    };

    struct RemovingListener final: public Listener {
        void onListen(void* argument) override;

        size_t calls = 0;
    };
}

StateListener::StateListener(Composer& composer_)
    : composer(composer_)
{
}

void StateListener::onListen(void* argument) {
    extras = composer.extras.store;
    columns = composer.geometry.columns;
    rows = composer.geometry.rows;
    pixelWidth = composer.geometry.pixelWidth;
    pixelHeight = composer.geometry.pixelHeight;
    contentScale = composer.contentScale;
    ++calls;
    argumentWasNull = argument == nullptr;
}

void RemovingListener::onListen(void*) {
    ++calls;
    unlink();
}

STD_TEST_SUITE(Composer) {
    STD_TEST(ConstructsCoreComponents) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());

        STD_INSIST(composer.pool == pool.mutPtr());
        STD_INSIST(composer.extras.store != nullptr);
        STD_INSIST(composer.smallObjects != nullptr);
        STD_INSIST(composer.input != nullptr);
        STD_INSIST(composer.inputBindings != nullptr);
        STD_INSIST(composer.inputHandlers.front() != composer.inputHandlers.end());
        STD_INSIST(composer.fontResolvers.front() != composer.fontResolvers.end());
    }

    STD_TEST(PublishesContentScaleOnlyAfterChange) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        StateListener listener(composer);
        composer.contentScaleChangedListeners.pushBack(&listener);

        composer.setContentScale(1.5f);

        STD_INSIST(composer.contentScale == 1.5f);
        STD_INSIST(listener.contentScale == 1.5f);
        STD_INSIST(listener.calls == 1);
        STD_INSIST(listener.argumentWasNull);

        composer.setContentScale(1.5f);

        STD_INSIST(listener.calls == 1);
    }

    STD_TEST(DerivesPhysicalBorderFromSnapshotAndScale) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 7;
        composer.setOptions(&options);

        STD_INSIST(composer.layout.borderPixels == 7);

        composer.setContentScale(1.5f);

        STD_INSIST(composer.layout.borderPixels == 11);
    }

    STD_TEST(SeparatesTerminalResizeFromWindowLayout) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        StateListener resized(composer);
        StateListener placed(composer);
        composer.resizedListeners.pushBack(&resized);
        composer.layoutChangedListeners.pushBack(&placed);
        composer.installVtHost();
        composer.geometry.setCellPixelSize(8, 16);
        const u16 width = 2 * composer.layout.borderPixels + 80 + 3;
        const u16 height = 2 * composer.layout.borderPixels + 64 + 7;

        composer.resizeWindow(width, height);

        STD_INSIST(resized.calls == 1 && placed.calls == 1);
        STD_INSIST(resized.columns == 10 && resized.rows == 4);
        STD_INSIST(resized.pixelWidth == 80 && resized.pixelHeight == 64);
        STD_INSIST(composer.layout.originX == composer.layout.borderPixels + 1);
        STD_INSIST(composer.layout.originY == composer.layout.borderPixels + 3);

        composer.resizeWindow(width, height);
        STD_INSIST(resized.calls == 1 && placed.calls == 1);

        // The origin moves, but the terminal dimensions and its resize
        // notification remain unchanged.
        composer.resizeWindow(width + 1, height);
        STD_INSIST(resized.calls == 1 && placed.calls == 2);
        STD_INSIST(composer.layout.originX == composer.layout.borderPixels + 2);
        STD_INSIST(composer.geometry.pixelWidth == 80);

        composer.resizeWindow(width - 3, height - 7);
        STD_INSIST(resized.calls == 1 && placed.calls == 3);
        STD_INSIST(composer.layout.originX == composer.layout.borderPixels);
        STD_INSIST(composer.layout.originY == composer.layout.borderPixels);

        composer.resizeWindow(width + 8, height);
        STD_INSIST(resized.calls == 2 && placed.calls == 4);
        STD_INSIST(resized.columns == 11 && resized.pixelWidth == 88);
    }

    STD_TEST(PublishesCellExtraReplacement) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        StateListener listener(composer);
        composer.extras.changedListeners.pushBack(&listener);
        auto* first = reinterpret_cast<CellExtraStore*>(uintptr_t(1));
        auto* second = reinterpret_cast<CellExtraStore*>(uintptr_t(2));

        composer.extras.replace(first);

        STD_INSIST(listener.calls == 1);
        STD_INSIST(listener.extras == first);

        composer.extras.replace(first);

        STD_INSIST(listener.calls == 1);

        composer.extras.replace(second);

        STD_INSIST(listener.calls == 2);
        STD_INSIST(listener.extras == second);
    }

    STD_TEST(ListenerMayRemoveItselfDuringPublication) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        RemovingListener removing;
        StateListener trailing(composer);
        composer.contentScaleChangedListeners.pushBack(&removing);
        composer.contentScaleChangedListeners.pushBack(&trailing);

        composer.setContentScale(1.25f);
        composer.setContentScale(1.5f);

        STD_INSIST(removing.calls == 1);
        STD_INSIST(trailing.calls == 2);
    }
}
