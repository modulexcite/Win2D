// Copyright (c) Microsoft Corporation. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use these files except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations
// under the License.

#pragma once

#include <lib/xaml/RecreatableDeviceManager.impl.h>

#include "mocks/MockCanvasDeviceActivationFactory.h"
#include "mocks/MockWindow.h"
#include "StubDispatcher.h"

template<typename TRAITS>
class BaseControlTestAdapter : public TRAITS::adapter_t
{
    ComPtr<StubDispatcher> m_uiThreadDispatcher;
    ComPtr<MockWindow> m_mockWindow;
    bool m_hasUIThreadAccess;

public:
    ComPtr<MockEventSource<DpiChangedEventHandler>> DpiChangedEventSource;
    ComPtr<MockEventSource<IEventHandler<SuspendingEventArgs*>>> SuspendingEventSource;
    ComPtr<MockEventSource<IEventHandler<IInspectable*>>> ResumingEventSource;
    CALL_COUNTER_WITH_MOCK(CreateRecreatableDeviceManagerMethod, std::unique_ptr<IRecreatableDeviceManager<TRAITS>>());

    ComPtr<MockCanvasDeviceActivationFactory> DeviceFactory;

    float LogicalDpi;
    bool DesignModeEnabled;

    BaseControlTestAdapter()
        : DeviceFactory(Make<MockCanvasDeviceActivationFactory>())
        , m_uiThreadDispatcher(Make<StubDispatcher>())
        , m_mockWindow(Make<MockWindow>())
        , m_hasUIThreadAccess(true)
        , DpiChangedEventSource(Make<MockEventSource<DpiChangedEventHandler>>(L"DpiChanged"))
        , SuspendingEventSource(Make<MockEventSource<IEventHandler<SuspendingEventArgs*>>>(L"Suspending"))
        , ResumingEventSource(Make<MockEventSource<IEventHandler<IInspectable*>>>(L"Resuming"))
        , LogicalDpi(DEFAULT_DPI)
        , DesignModeEnabled(false)
    {
        CreateRecreatableDeviceManagerMethod.AllowAnyCall();
        DeviceFactory->ActivateInstanceMethod.AllowAnyCall();

        m_uiThreadDispatcher->get_HasThreadAccessMethod.AllowAnyCall(
            [=](boolean* out)
            {
                *out = m_hasUIThreadAccess;
                
                return S_OK;
            });

        m_mockWindow->get_DispatcherMethod.AllowAnyCall(
            [=](ICoreDispatcher** out)
            {
                return m_uiThreadDispatcher.CopyTo(out);
            });
    }

    void TickUiThread()
    {
        m_uiThreadDispatcher->TickAll();
    }

    bool HasPendingActionsOnUiThread()
    {
        return m_uiThreadDispatcher->HasPendingActions();
    }

    virtual bool IsDesignModeEnabled() override
    {
        return DesignModeEnabled;
    }

    virtual ComPtr<IInspectable> CreateUserControl(IInspectable* canvasControl) override
    {
        return As<IInspectable>(Make<StubUserControl>());
    }

    virtual RegisteredEvent AddApplicationSuspendingCallback(IEventHandler<SuspendingEventArgs*>* value) override
    {
        return SuspendingEventSource->Add(value);
    }

    virtual RegisteredEvent AddApplicationResumingCallback(IEventHandler<IInspectable*>* value) override
    {
        return ResumingEventSource->Add(value);
    }

    virtual float GetLogicalDpi() override
    {
        return LogicalDpi;
    }

    virtual RegisteredEvent AddDpiChangedCallback(DpiChangedEventHandler* value) override
    {
        return DpiChangedEventSource->Add(value);
    }

    virtual RegisteredEvent AddVisibilityChangedCallback(IWindowVisibilityChangedEventHandler* value, IWindow* window) override
    {
        MockWindow* mockWindow = static_cast<MockWindow*>(window);
        return mockWindow->VisibilityChangedEventSource->Add(value);
    }

    void RaiseDpiChangedEvent()
    {
        ThrowIfFailed(DpiChangedEventSource->InvokeAll(nullptr, nullptr));
    }

    virtual ComPtr<IWindow> GetWindowOfCurrentThread() override
    {
        return m_mockWindow;
    }

    ComPtr<MockWindow> GetCurrentMockWindow()
    {
        return m_mockWindow;
    }

    virtual std::unique_ptr<IRecreatableDeviceManager<TRAITS>> CreateRecreatableDeviceManager() override
    {
        auto manager = CreateRecreatableDeviceManagerMethod.WasCalled();
        if (manager)
            return manager;

        return std::make_unique<RecreatableDeviceManager<TRAITS>>(DeviceFactory.Get());
    }

    void SetHasUIThreadAccess(bool value)
    {
        m_hasUIThreadAccess = value;
    }
};

#define VERIFY_THREADING_RESTRICTION(EXPECTED_HR, FUNC) \
    f.Adapter->SetHasUIThreadAccess(false);             \
    Assert::AreEqual(EXPECTED_HR, FUNC);                \
    f.Adapter->SetHasUIThreadAccess(true);              \
    Assert::AreEqual(S_OK, FUNC);                      
