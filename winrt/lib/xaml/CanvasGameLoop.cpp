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

#include "pch.h"

#include "CanvasGameLoop.h"

using namespace ::ABI::Microsoft::Graphics::Canvas;
using namespace ::ABI::Microsoft::Graphics::Canvas::UI;
using namespace ::ABI::Microsoft::Graphics::Canvas::UI::Xaml;
using namespace ::Microsoft::WRL::Wrappers;

CanvasGameLoop::CanvasGameLoop(ComPtr<IAsyncAction>&& action, ComPtr<ICoreDispatcher>&& dispatcher, ComPtr<AnimatedControlInput> input)
    : m_threadAction(std::move(action))
    , m_dispatcher(std::move(dispatcher))
{ 
    // Set the input by dispatching to the game thread
    auto handler = Callback<AddFtmBase<IDispatchedHandler>::Type>(
        [input] ()
        {
            return ExceptionBoundary([&] { input->SetSource(); });
        });
    CheckMakeResult(handler);

    ComPtr<IAsyncAction> ignoredAction;
    ThrowIfFailed(m_dispatcher->RunAsync(CoreDispatcherPriority_Normal, handler.Get(), &ignoredAction));

    // When the game thread exits we need to unset the input
    auto onThreadExit = Callback<AddFtmBase<IAsyncActionCompletedHandler>::Type>(
        [input] (IAsyncAction*, AsyncStatus)
        {
            return ExceptionBoundary([&] { input->RemoveSource(); });
        });
    CheckMakeResult(onThreadExit);
    ThrowIfFailed(m_threadAction->put_Completed(onThreadExit.Get()));
}

CanvasGameLoop::~CanvasGameLoop()
{
    // Kill the game thread by stopping the dispatcher
    As<ICoreDispatcherWithTaskPriority>(m_dispatcher)->StopProcessEvents();
}

void CanvasGameLoop::StartTickLoop(
    CanvasAnimatedControl* control,
    std::function<bool(CanvasAnimatedControl*)> const& tickFn,
    std::function<void(CanvasAnimatedControl*)> const& completedFn)
{
    auto lock = Lock(m_mutex);

    assert(!m_tickLoopAction);

    std::weak_ptr<CanvasGameLoop> weakSelf(shared_from_this());
    auto weakControl = AsWeak(control);

    m_tickHandler = Callback<AddFtmBase<IDispatchedHandler>::Type>(
        [weakSelf, weakControl, tickFn] () mutable
        {
            return ExceptionBoundary(
                [&]
                {
                    auto self = weakSelf.lock();

                    if (!self)
                        return;

                    self->m_tickLoopShouldContinue = false;

                    auto strongControl = LockWeakRef<ICanvasAnimatedControl>(weakControl);
                    auto control = static_cast<CanvasAnimatedControl*>(strongControl.Get());

                    if (!control)
                        return;

                    self->m_tickLoopShouldContinue = tickFn(control);
                });
        });
    CheckMakeResult(m_tickHandler);

    m_tickCompletedHandler = Callback<AddFtmBase<IAsyncActionCompletedHandler>::Type>(
        [weakSelf, weakControl, completedFn] (IAsyncAction*, AsyncStatus status) mutable
        {
            return ExceptionBoundary(
                [&] 
                {
                    auto self = weakSelf.lock();

                    if (!self)
                        return;

                    auto strongControl = LockWeakRef<ICanvasAnimatedControl>(weakControl);
                    auto control = static_cast<CanvasAnimatedControl*>(strongControl.Get());
                
                    auto lock = Lock(self->m_mutex);

                    if (self->m_tickLoopShouldContinue)
                    {
#ifdef NDEBUG
                        UNREFERENCED_PARAMETER(status);
#else
                        assert(status == AsyncStatus::Completed);
#endif

                        self->ScheduleTick(lock);
                    }
                    else
                    {
                        completedFn(control);
                    }
                });
        });
    CheckMakeResult(m_tickCompletedHandler);

    ScheduleTick(lock);
}

void CanvasGameLoop::ScheduleTick(Lock const& lock)
{
    MustOwnLock(lock);

    ThrowIfFailed(m_dispatcher->RunAsync(CoreDispatcherPriority_Normal, m_tickHandler.Get(), &m_tickLoopAction));
    ThrowIfFailed(m_tickLoopAction->put_Completed(m_tickCompletedHandler.Get()));
    
}

void CanvasGameLoop::TakeTickLoopState(bool* isRunning, ComPtr<IAsyncInfo>* errorInfo)
{
    *isRunning = false;
    *errorInfo = nullptr;

    auto lock = Lock(m_mutex);

    auto info = MaybeAs<IAsyncInfo>(m_tickLoopAction);

    if (!info)
    {
        return;
    }

    AsyncStatus status;
    ThrowIfFailed(info->get_Status(&status));

    switch (status)
    {
    case AsyncStatus::Started:
        *isRunning = true;
        break;

    case AsyncStatus::Completed:
        m_tickLoopAction.Reset();
        break;

    default:
        *errorInfo = info;
        m_tickLoopAction.Reset();
        break;
    }
}
