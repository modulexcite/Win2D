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

TEST_CLASS(CanvasDeviceTests)
{
public:
    std::shared_ptr<TestDeviceResourceCreationAdapter> m_resourceCreationAdapter;
    std::shared_ptr<CanvasDeviceManager> m_deviceManager;

    TEST_METHOD_INITIALIZE(Reset)
    {
        m_resourceCreationAdapter = std::make_shared<TestDeviceResourceCreationAdapter>();
        m_deviceManager = std::make_shared<CanvasDeviceManager>(m_resourceCreationAdapter);
    }


    void AssertDeviceManagerRoundtrip(ICanvasDevice* expectedCanvasDevice)
    {
        ComPtr<ICanvasDeviceInternal> canvasDeviceInternal;
        ThrowIfFailed(expectedCanvasDevice->QueryInterface(canvasDeviceInternal.GetAddressOf()));

        auto d2dDevice = canvasDeviceInternal->GetD2DDevice();
        auto actualCanvasDevice = m_deviceManager->GetOrCreate(d2dDevice.Get());
        
        Assert::AreEqual<ICanvasDevice*>(expectedCanvasDevice, actualCanvasDevice.Get());
    }

    TEST_METHOD_EX(CanvasDevice_Implements_Expected_Interfaces)
    {
        auto canvasDevice = m_deviceManager->Create(CanvasDebugLevel::None, CanvasHardwareAcceleration::Auto);

        ASSERT_IMPLEMENTS_INTERFACE(canvasDevice, ICanvasDevice);
        ASSERT_IMPLEMENTS_INTERFACE(canvasDevice, ABI::Windows::Foundation::IClosable);
        ASSERT_IMPLEMENTS_INTERFACE(canvasDevice, ICanvasResourceWrapperNative);
        ASSERT_IMPLEMENTS_INTERFACE(canvasDevice, ICanvasDeviceInternal);
    }

    TEST_METHOD_EX(CanvasDevice_Defaults_Roundtrip)
    {
        auto canvasDevice = m_deviceManager->Create(CanvasDebugLevel::None, CanvasHardwareAcceleration::Auto);
        Assert::IsNotNull(canvasDevice.Get());

        Assert::AreEqual(1, m_resourceCreationAdapter->m_numD2DFactoryCreationCalls);
        Assert::AreEqual(CanvasDebugLevel::None, m_resourceCreationAdapter->m_debugLevel);
        Assert::AreEqual(1, m_resourceCreationAdapter->m_numD3dDeviceCreationCalls);
        Assert::AreEqual(CanvasHardwareAcceleration::On, m_resourceCreationAdapter->m_retrievableHarwareAcceleration); // Hardware is the default, and should be used here.

        AssertDeviceManagerRoundtrip(canvasDevice.Get());
    }

    TEST_METHOD_EX(CanvasDevice_DebugLevels)
    {
        CanvasDebugLevel cases[] = { CanvasDebugLevel::None, CanvasDebugLevel::Error, CanvasDebugLevel::Warning, CanvasDebugLevel::Information };
        for (auto expectedDebugLevel : cases)
        {
            Reset();

            auto canvasDevice = m_deviceManager->Create(expectedDebugLevel, CanvasHardwareAcceleration::Auto);
            Assert::IsNotNull(canvasDevice.Get());

            Assert::AreEqual(1, m_resourceCreationAdapter->m_numD3dDeviceCreationCalls);
            Assert::AreEqual(expectedDebugLevel, m_resourceCreationAdapter->m_debugLevel);
            AssertDeviceManagerRoundtrip(canvasDevice.Get());
        }

        // Try an invalid debug level
        Reset();
        ExpectHResultException(E_INVALIDARG,
            [&]
            {
                m_deviceManager->Create(
                    static_cast<CanvasDebugLevel>(1234),
                    CanvasHardwareAcceleration::Auto);
            });
    }

    TEST_METHOD_EX(CanvasDevice_HardwareAcceleration)
    {
        CanvasHardwareAcceleration cases[] = { CanvasHardwareAcceleration::On, CanvasHardwareAcceleration::Off };
        for (auto expectedHardwareAcceleration : cases)
        {
            Reset();
            
            auto canvasDevice = m_deviceManager->Create(
                CanvasDebugLevel::Information, 
                expectedHardwareAcceleration);

            Assert::IsNotNull(canvasDevice.Get());

            // Verify HW acceleration property getter returns the right thing
            CanvasHardwareAcceleration hardwareAccelerationActual;
            canvasDevice->get_HardwareAcceleration(&hardwareAccelerationActual);
            Assert::AreEqual(expectedHardwareAcceleration, hardwareAccelerationActual);

            // Ensure that the getter returns E_INVALIDARG with null ptr.
            Assert::AreEqual(E_INVALIDARG, canvasDevice->get_HardwareAcceleration(NULL));

            Assert::AreEqual(1, m_resourceCreationAdapter->m_numD2DFactoryCreationCalls);
            Assert::AreEqual(CanvasDebugLevel::Information, m_resourceCreationAdapter->m_debugLevel);
            AssertDeviceManagerRoundtrip(canvasDevice.Get());
        }

        // Try some invalid options
        Reset();

        CanvasHardwareAcceleration invalidCases[] = 
            {
                CanvasHardwareAcceleration::Unknown,
                static_cast<CanvasHardwareAcceleration>(0x5678)
            };

        for (auto invalidCase : invalidCases)
        {
            ExpectHResultException(E_INVALIDARG,
                [&] 
                { 
                    m_deviceManager->Create(
                        CanvasDebugLevel::None,
                        invalidCase);
                });
        }
    }

    TEST_METHOD_EX(CanvasDevice_Create_With_Specific_Direct3DDevice)
    {
        ComPtr<StubD3D11Device> stubD3D11Device = Make<StubD3D11Device>();

        ComPtr<IDirect3DDevice> stubDirect3DDevice;
        ThrowIfFailed(CreateDirect3D11DeviceFromDXGIDevice(stubD3D11Device.Get(), &stubDirect3DDevice));

        auto canvasDevice = m_deviceManager->Create(
            CanvasDebugLevel::None, 
            stubDirect3DDevice.Get());
        Assert::IsNotNull(canvasDevice.Get());

        // A D2D device should still have been created
        Assert::AreEqual(1, m_resourceCreationAdapter->m_numD2DFactoryCreationCalls);
        Assert::AreEqual(CanvasDebugLevel::None, m_resourceCreationAdapter->m_debugLevel);

        // But not a D3D device.
        Assert::AreEqual(0, m_resourceCreationAdapter->m_numD3dDeviceCreationCalls);

        AssertDeviceManagerRoundtrip(canvasDevice.Get());

        CanvasHardwareAcceleration hardwareAcceleration{};
        ThrowIfFailed(canvasDevice->get_HardwareAcceleration(&hardwareAcceleration));
        Assert::AreEqual(CanvasHardwareAcceleration::Unknown, hardwareAcceleration);

        // Try a NULL Direct3DDevice. 
        ExpectHResultException(E_INVALIDARG,
            [&] { m_deviceManager->Create(CanvasDebugLevel::None, nullptr); });
    }

    TEST_METHOD_EX(CanvasDevice_Create_From_D2DDevice)
    {
        auto d2dDevice = Make<MockD2DDevice>(Make<MockD2DFactory>().Get());

        auto canvasDevice = m_deviceManager->GetOrCreate(d2dDevice.Get());
        Assert::IsNotNull(canvasDevice.Get());

        // Nothing should have been created
        Assert::AreEqual(0, m_resourceCreationAdapter->m_numD2DFactoryCreationCalls);
        Assert::AreEqual(0, m_resourceCreationAdapter->m_numD3dDeviceCreationCalls);

        AssertDeviceManagerRoundtrip(canvasDevice.Get());

        CanvasHardwareAcceleration hardwareAcceleration{};
        ThrowIfFailed(canvasDevice->get_HardwareAcceleration(&hardwareAcceleration));
        Assert::AreEqual(CanvasHardwareAcceleration::Unknown, hardwareAcceleration);
    }

    TEST_METHOD_EX(CanvasDevice_Closed)
    {
        auto canvasDevice = m_deviceManager->Create(CanvasDebugLevel::None, CanvasHardwareAcceleration::On);
        Assert::IsNotNull(canvasDevice.Get());

        Assert::AreEqual(S_OK, canvasDevice->Close());

        ComPtr<IDXGIDevice> dxgiDevice;
        Assert::AreEqual(RO_E_CLOSED, canvasDevice->GetInterface(IID_PPV_ARGS(&dxgiDevice)));

        CanvasHardwareAcceleration hardwareAccelerationActual = static_cast<CanvasHardwareAcceleration>(1);
        Assert::AreEqual(RO_E_CLOSED, canvasDevice->get_HardwareAcceleration(&hardwareAccelerationActual));
    }

    ComPtr<ID2D1Device1> GetD2DDevice(ComPtr<ICanvasDevice> const& canvasDevice)
    {
        ComPtr<ICanvasDeviceInternal> canvasDeviceInternal;
        Assert::AreEqual(S_OK, canvasDevice.As(&canvasDeviceInternal));
        return canvasDeviceInternal->GetD2DDevice();
    }

    TEST_METHOD_EX(CanvasDevice_HwSwFallback)
    {
        Reset();

        int d3dDeviceCreationCount = 0;

        // Default canvas device should be hardware.
        auto canvasDevice = m_deviceManager->Create(
            CanvasDebugLevel::None, 
            CanvasHardwareAcceleration::Auto);
        d3dDeviceCreationCount++;

        Assert::AreEqual(CanvasHardwareAcceleration::On, m_resourceCreationAdapter->m_retrievableHarwareAcceleration);
        Assert::AreEqual(d3dDeviceCreationCount, m_resourceCreationAdapter->m_numD3dDeviceCreationCalls);

        // Now disable the hardware path.
        m_resourceCreationAdapter->SetHardwareEnabled(false);

        {
            // Ensure the fallback works.
            canvasDevice = m_deviceManager->Create(
                CanvasDebugLevel::None,
                CanvasHardwareAcceleration::Auto);
            d3dDeviceCreationCount++;

            // Ensure the software path was used.
            Assert::AreEqual(CanvasHardwareAcceleration::Off, m_resourceCreationAdapter->m_retrievableHarwareAcceleration);
            Assert::AreEqual(d3dDeviceCreationCount, m_resourceCreationAdapter->m_numD3dDeviceCreationCalls);

            // Ensure the HardwareAcceleration property getter returns the right thing.
            CanvasHardwareAcceleration hardwareAcceleration;
            canvasDevice->get_HardwareAcceleration(&hardwareAcceleration);
            Assert::AreEqual(CanvasHardwareAcceleration::Off, hardwareAcceleration);
        }
        {
            // Re-create another whole device with the hardware path on, ensuring there isn't some weird statefulness problem.
            m_resourceCreationAdapter->SetHardwareEnabled(true);
            canvasDevice = m_deviceManager->Create(
                CanvasDebugLevel::None,
                CanvasHardwareAcceleration::Auto);
            d3dDeviceCreationCount++;

            // Ensure the hardware path was used.
            Assert::AreEqual(CanvasHardwareAcceleration::On, m_resourceCreationAdapter->m_retrievableHarwareAcceleration);
            Assert::AreEqual(d3dDeviceCreationCount, m_resourceCreationAdapter->m_numD3dDeviceCreationCalls);

            // Ensure the HardwareAcceleration property getter returns HW again.
            CanvasHardwareAcceleration hardwareAcceleration;
            canvasDevice->get_HardwareAcceleration(&hardwareAcceleration);
            Assert::AreEqual(CanvasHardwareAcceleration::On, hardwareAcceleration);
        }
    }

    TEST_METHOD_EX(CanvasDeviceManager_Create_GetOrCreate_Returns_Same_Instance)
    {
        auto expectedCanvasDevice = m_deviceManager->Create(CanvasDebugLevel::None, CanvasHardwareAcceleration::On);

        //
        // Create, followed by GetOrCreate on the same d2d device should give
        // back the same CanvasDevice.
        //

        auto d2dDevice = expectedCanvasDevice->GetD2DDevice();

        auto actualCanvasDevice = m_deviceManager->GetOrCreate(d2dDevice.Get());

        Assert::AreEqual<ICanvasDevice*>(expectedCanvasDevice.Get(), actualCanvasDevice.Get());

        //
        // Destroying these original CanvasDevices, and the GetOrCreate using
        // the same d2d device should give back a new, different, CanvasDevice.
        //

        WeakRef weakExpectedCanvasDevice;
        ThrowIfFailed(AsWeak(expectedCanvasDevice.Get(), &weakExpectedCanvasDevice));
        expectedCanvasDevice.Reset();
        actualCanvasDevice.Reset();

        actualCanvasDevice = m_deviceManager->GetOrCreate(d2dDevice.Get());

        ComPtr<ICanvasDevice> unexpectedCanvasDevice;
        weakExpectedCanvasDevice.As(&unexpectedCanvasDevice);

        Assert::AreNotEqual<ICanvasDevice*>(unexpectedCanvasDevice.Get(), actualCanvasDevice.Get());
    }

    TEST_METHOD_EX(CanvasDevice_DeviceProperty)
    {
        auto device = m_deviceManager->Create(CanvasDebugLevel::None, CanvasHardwareAcceleration::On);

        Assert::AreEqual(E_INVALIDARG, device->get_Device(nullptr));

        ComPtr<ICanvasDevice> deviceVerify;
        ThrowIfFailed(device->get_Device(&deviceVerify));
        Assert::AreEqual(static_cast<ICanvasDevice*>(device.Get()), deviceVerify.Get());
    }
    
    TEST_METHOD_EX(CanvasDevice_MaximumBitmapSize_NullArg)
    {
        auto canvasDevice = m_deviceManager->Create(CanvasDebugLevel::None, CanvasHardwareAcceleration::On);

        Assert::AreEqual(E_INVALIDARG, canvasDevice->get_MaximumBitmapSizeInPixels(nullptr));
    }

    TEST_METHOD_EX(CanvasDevice_MaximumBitmapSize_Property)
    {
        auto d2dDevice = Make<MockD2DDevice>();

        const int32_t someSize = 1234567;

        d2dDevice->MockCreateDeviceContext =
            [&](D2D1_DEVICE_CONTEXT_OPTIONS, ID2D1DeviceContext1** value)
            {
                auto deviceContext = Make<StubD2DDeviceContext>(d2dDevice.Get());

                deviceContext->GetMaximumBitmapSizeMethod.SetExpectedCalls(1, [&]() { return someSize; });

                ThrowIfFailed(deviceContext.CopyTo(value));
            };

        auto canvasDevice = m_deviceManager->GetOrCreate(d2dDevice.Get());

        int32_t maximumBitmapSize;
        ThrowIfFailed(canvasDevice->get_MaximumBitmapSizeInPixels(&maximumBitmapSize));

        Assert::AreEqual(someSize, maximumBitmapSize);
    }

    TEST_METHOD_EX(CanvasDevice_CreateCommandList_ReturnsCommandListFromDeviceContext)
    {
        auto d2dDevice = Make<MockD2DDevice>();

        auto d2dCommandList = Make<MockD2DCommandList>();

        auto deviceContext = Make<StubD2DDeviceContext>(d2dDevice.Get());
        deviceContext->CreateCommandListMethod.SetExpectedCalls(1,
            [&](ID2D1CommandList** value)
            {
                return d2dCommandList.CopyTo(value);
            });

        d2dDevice->MockCreateDeviceContext =
            [&](D2D1_DEVICE_CONTEXT_OPTIONS, ID2D1DeviceContext1** value)
            {
                ThrowIfFailed(deviceContext.CopyTo(value));
            };

        auto canvasDevice = m_deviceManager->GetOrCreate(d2dDevice.Get());
        auto actualD2DCommandList = canvasDevice->CreateCommandList();

        Assert::IsTrue(IsSameInstance(d2dCommandList.Get(), actualD2DCommandList.Get()));
    }

    TEST_METHOD_EX(CanvasDevice_CreateRenderTarget_ReturnsBitmapCreatedWithCorrectProperties)
    {
        auto d2dDevice = Make<MockD2DDevice>();
        auto d2dBitmap = Make<MockD2DBitmap>();

        float anyWidth = 1.0f;
        float anyHeight = 2.0f;
        auto anyFormat = PIXEL_FORMAT(R16G16B16A16UIntNormalized);
        auto anyAlphaMode = CanvasAlphaMode::Ignore;
        float anyDpi = 3.0f;

        auto deviceContext = Make<StubD2DDeviceContext>(d2dDevice.Get());
        deviceContext->CreateBitmapMethod.SetExpectedCalls(1,
            [&] (D2D1_SIZE_U size, void const* sourceData, UINT32 pitch, D2D1_BITMAP_PROPERTIES1 const* bitmapProperties, ID2D1Bitmap1** bitmap)
            {
                Assert::AreEqual<int>(DipsToPixels(anyWidth, anyDpi), size.width);
                Assert::AreEqual<int>(DipsToPixels(anyHeight, anyDpi), size.height);
                Assert::IsNull(sourceData);
                Assert::AreEqual(0U, pitch);
                Assert::AreEqual(D2D1_BITMAP_OPTIONS_TARGET, bitmapProperties->bitmapOptions);
                Assert::AreEqual(anyDpi, bitmapProperties->dpiX);
                Assert::AreEqual(anyDpi, bitmapProperties->dpiY);
                Assert::AreEqual(static_cast<DXGI_FORMAT>(anyFormat), bitmapProperties->pixelFormat.format);
                Assert::AreEqual(ToD2DAlphaMode(anyAlphaMode), bitmapProperties->pixelFormat.alphaMode);
                return d2dBitmap.CopyTo(bitmap);
            });

        d2dDevice->MockCreateDeviceContext =
            [&](D2D1_DEVICE_CONTEXT_OPTIONS, ID2D1DeviceContext1** value)
            {
                ThrowIfFailed(deviceContext.CopyTo(value));
            };

        auto canvasDevice = m_deviceManager->GetOrCreate(d2dDevice.Get());
        auto actualBitmap = canvasDevice->CreateRenderTargetBitmap(anyWidth, anyHeight, anyDpi, anyFormat, anyAlphaMode);

        Assert::IsTrue(IsSameInstance(d2dBitmap.Get(), actualBitmap.Get()));
    }
};

TEST_CLASS(DefaultDeviceResourceCreationAdapterTests)
{
    //
    // This tests GetDxgiDevice against real-live D3D/D2D instances since it
    // relies on non-trivial interaction with these to behave as we expect.
    //
    TEST_METHOD_EX(GetDxgiDevice)
    {
        //
        // Set up
        //
        DefaultDeviceResourceCreationAdapter adapter;

        ComPtr<ID3D11Device> d3dDevice;
        if (!adapter.TryCreateD3DDevice(CanvasHardwareAcceleration::Off, &d3dDevice))
        {
            Assert::Fail(L"Failed to create d3d device");
        }

        ComPtr<IDXGIDevice3> dxgiDevice;
        ThrowIfFailed(d3dDevice.As(&dxgiDevice));

        auto factory = adapter.CreateD2DFactory(CanvasDebugLevel::None);
        ComPtr<ID2D1Device1> d2dDevice;
        ThrowIfFailed(factory->CreateDevice(dxgiDevice.Get(), &d2dDevice));

        //
        // Test
        //
        auto actualDxgiDevice = adapter.GetDxgiDevice(d2dDevice.Get());

        Assert::AreEqual(dxgiDevice.Get(), actualDxgiDevice.Get());
    }
};

static const HRESULT deviceRemovedHResults[] = {
    DXGI_ERROR_DEVICE_HUNG,
    DXGI_ERROR_DEVICE_REMOVED,
    DXGI_ERROR_DEVICE_RESET,
    DXGI_ERROR_DRIVER_INTERNAL_ERROR,
    DXGI_ERROR_INVALID_CALL,
    D2DERR_RECREATE_TARGET
};

TEST_CLASS(CanvasDeviceLostTests)
{
    std::shared_ptr<TestDeviceResourceCreationAdapter> m_resourceCreationAdapter;
    std::shared_ptr<CanvasDeviceManager> m_deviceManager;

public:

    TEST_METHOD_INITIALIZE(Reset)
    {
        m_resourceCreationAdapter = std::make_shared<TestDeviceResourceCreationAdapter>();
        m_deviceManager = std::make_shared<CanvasDeviceManager>(m_resourceCreationAdapter);
    }

    TEST_METHOD_EX(CanvasDeviceLostTests_Closed)
    {
        auto canvasDevice = m_deviceManager->Create(CanvasDebugLevel::None, CanvasHardwareAcceleration::On);

        Assert::AreEqual(S_OK, canvasDevice->Close());

        EventRegistrationToken token{};
        MockEventHandler<DeviceLostHandlerType> dummyDeviceLostHandler(L"DeviceLost");
        Assert::AreEqual(RO_E_CLOSED, canvasDevice->add_DeviceLost(dummyDeviceLostHandler.Get(), &token));

        // remove_DeviceLost is intended to not check if the object is closed, and like all EventSource
        // events it returns success if you try and remove an unregistered token.
        Assert::AreEqual(S_OK, canvasDevice->remove_DeviceLost(token));

        boolean b;
        Assert::AreEqual(RO_E_CLOSED, canvasDevice->IsDeviceLost(0, &b));

        Assert::AreEqual(RO_E_CLOSED, canvasDevice->RaiseDeviceLost());

    }

    TEST_METHOD_EX(CanvasDeviceLostTests_NullArgs)
    {
        auto canvasDevice = m_deviceManager->Create(CanvasDebugLevel::None, CanvasHardwareAcceleration::On);

        EventRegistrationToken token{};
        MockEventHandler<DeviceLostHandlerType> dummyDeviceLostHandler(L"DeviceLost");
        Assert::AreEqual(E_INVALIDARG, canvasDevice->add_DeviceLost(nullptr, &token));
        Assert::AreEqual(E_INVALIDARG, canvasDevice->add_DeviceLost(dummyDeviceLostHandler.Get(), nullptr));
        Assert::AreEqual(E_INVALIDARG, canvasDevice->IsDeviceLost(0, nullptr));
    }

    class DeviceLostResourceCreationAdapter : public TestDeviceResourceCreationAdapter
    {
        virtual ComPtr<StubD3D11Device> CreateStubD3D11Device() override
        {
            auto stubD3DDevice = Make<StubD3D11Device>();

            stubD3DDevice->GetDeviceRemovedReasonMethod.AllowAnyCall(
                [] { return DXGI_ERROR_DEVICE_REMOVED; });

            return stubD3DDevice;
        }
    };

    class DeviceLostFixture
    {
        std::shared_ptr<DeviceLostResourceCreationAdapter> m_resourceCreationAdapter;

    public:
        std::shared_ptr<CanvasDeviceManager> DeviceManager;

        DeviceLostFixture()
            : m_resourceCreationAdapter(std::make_shared<DeviceLostResourceCreationAdapter>())
            , DeviceManager(std::make_shared<CanvasDeviceManager>(m_resourceCreationAdapter))
        {
        }
    };

    TEST_METHOD_EX(CanvasDeviceLostTests_IsDeviceLost_DeviceRemovedHr_DeviceIsLost_ReturnsTrue)
    {
        DeviceLostFixture f;
        auto canvasDevice = f.DeviceManager->Create(CanvasDebugLevel::None, CanvasHardwareAcceleration::On);

        for (HRESULT hr : deviceRemovedHResults)
        {
            boolean isDeviceLost;
            Assert::AreEqual(S_OK, canvasDevice->IsDeviceLost(hr, &isDeviceLost));
            Assert::IsTrue(!!isDeviceLost);
        }
    }

    TEST_METHOD_EX(CanvasDeviceLostTests_IsDeviceLost_SomeArbitraryHr_DeviceIsLost_ReturnsFalse)
    {
        DeviceLostFixture f;
        auto canvasDevice = f.DeviceManager->Create(CanvasDebugLevel::None, CanvasHardwareAcceleration::On);

        boolean isDeviceLost;
        Assert::AreEqual(S_OK, canvasDevice->IsDeviceLost(E_INVALIDARG, &isDeviceLost));
        Assert::IsFalse(!!isDeviceLost);
    }

    TEST_METHOD_EX(CanvasDeviceLostTests_IsDeviceLost_DeviceRemovedHr_DeviceNotActuallyLost_ReturnsFalse)
    {
        auto canvasDevice = m_deviceManager->Create(CanvasDebugLevel::None, CanvasHardwareAcceleration::On);

        for (HRESULT hr : deviceRemovedHResults)
        {
            boolean isDeviceLost;
            Assert::AreEqual(S_OK, canvasDevice->IsDeviceLost(hr, &isDeviceLost));
            Assert::IsFalse(!!isDeviceLost);
        }
    }

    TEST_METHOD_EX(CanvasDeviceLostTests_IsDeviceLost_SomeArbitraryHr_DeviceNotActuallyLost_ReturnsFalse)
    {
        auto canvasDevice = m_deviceManager->Create(CanvasDebugLevel::None, CanvasHardwareAcceleration::On);

        boolean isDeviceLost;
        Assert::AreEqual(S_OK, canvasDevice->IsDeviceLost(E_INVALIDARG, &isDeviceLost));
        Assert::IsFalse(!!isDeviceLost);
    }

    TEST_METHOD_EX(CanvasDeviceLostTests_RaiseDeviceLost_RaisesSubscribedHandlers_DeviceActuallyLost)
    {
        DeviceLostFixture f;
        auto canvasDevice = f.DeviceManager->Create(CanvasDebugLevel::None, CanvasHardwareAcceleration::On);

        MockEventHandler<DeviceLostHandlerType> deviceLostHandler(L"DeviceLost");
        deviceLostHandler.SetExpectedCalls(1);

        EventRegistrationToken token{};
        Assert::AreEqual(S_OK, canvasDevice->add_DeviceLost(deviceLostHandler.Get(), &token));

        Assert::AreEqual(S_OK, canvasDevice->RaiseDeviceLost());
    }

    TEST_METHOD_EX(CanvasDeviceLostTests_RaiseDeviceLost_RaisesSubscribedHandlers_DeviceNotActuallyLost)
    {
        //
        // The unit tests testing the DeviceLost event do not exhaustively test 
        // everything concerning  adding/removing events, because DeviceLost is 
        // implemented directly on top of EventSource<...>, which 
        // already has coverage elsewhere.
        //
        auto canvasDevice = m_deviceManager->Create(CanvasDebugLevel::None, CanvasHardwareAcceleration::On);

        MockEventHandler<DeviceLostHandlerType> deviceLostHandler(L"DeviceLost");
        deviceLostHandler.SetExpectedCalls(1);

        EventRegistrationToken token{};
        Assert::AreEqual(S_OK, canvasDevice->add_DeviceLost(deviceLostHandler.Get(), &token));

        Assert::AreEqual(S_OK, canvasDevice->RaiseDeviceLost());
    }

    TEST_METHOD_EX(CanvasDeviceLostTests_RemoveEventThen_RaiseDeviceLost_DoesNotInvokeHandler)
    {
        auto canvasDevice = m_deviceManager->Create(CanvasDebugLevel::None, CanvasHardwareAcceleration::On);

        MockEventHandler<DeviceLostHandlerType> deviceLostHandler(L"DeviceLost");
        deviceLostHandler.SetExpectedCalls(0);

        EventRegistrationToken token{};
        Assert::AreEqual(S_OK, canvasDevice->add_DeviceLost(deviceLostHandler.Get(), &token));
        Assert::AreEqual(S_OK, canvasDevice->remove_DeviceLost(token));

        Assert::AreEqual(S_OK, canvasDevice->RaiseDeviceLost());
    }

    TEST_METHOD_EX(CanvasDeviceLostTests_RaiseDeviceLost_HasCorrectSenderAndArgs)
    {
        DeviceLostFixture f;
        auto canvasDevice = f.DeviceManager->Create(CanvasDebugLevel::None, CanvasHardwareAcceleration::On);

        MockEventHandler<DeviceLostHandlerType> deviceLostHandler(L"DeviceLost");
        deviceLostHandler.SetExpectedCalls(1, 
            [&](ICanvasDevice* sender, IInspectable* args)
            {
                Assert::AreEqual(static_cast<ICanvasDevice*>(canvasDevice.Get()), sender);
                Assert::IsNull(args);
                return S_OK;
            });

        EventRegistrationToken token{};
        Assert::AreEqual(S_OK, canvasDevice->add_DeviceLost(deviceLostHandler.Get(), &token));

        Assert::AreEqual(S_OK, canvasDevice->RaiseDeviceLost());
    }

    TEST_METHOD_EX(CanvasDeviceLostTests_RaiseDeviceLost_ExceptionFromHandlerIsPropagated)
    {
        DeviceLostFixture f;
        auto canvasDevice = f.DeviceManager->Create(CanvasDebugLevel::None, CanvasHardwareAcceleration::On);

        MockEventHandler<DeviceLostHandlerType> deviceLostHandler(L"DeviceLost");
        deviceLostHandler.SetExpectedCalls(1, 
            [&](ICanvasDevice* sender, IInspectable* args)
            {
                return E_UNEXPECTED;
            });

        EventRegistrationToken token{};
        Assert::AreEqual(S_OK, canvasDevice->add_DeviceLost(deviceLostHandler.Get(), &token));

        Assert::AreEqual(E_UNEXPECTED, canvasDevice->RaiseDeviceLost());
    }
};

CanvasHardwareAcceleration allHardwareAccelerationTypes[] = {
    CanvasHardwareAcceleration::Auto,
    CanvasHardwareAcceleration::On,
    CanvasHardwareAcceleration::Off
};

TEST_CLASS(CanvasGetSharedDeviceTests)
{
public:

    class GetSharedDevice_Adapter : public TestDeviceResourceCreationAdapter
    {
        int m_deviceLostCounter;
        bool m_canCreateDevices;

    public:
        CALL_COUNTER_WITH_MOCK(CreateStubD3D11DeviceMethod, ComPtr<StubD3D11Device>());

        GetSharedDevice_Adapter()
            : m_canCreateDevices(true)
        {
            CreateStubD3D11DeviceMethod.AllowAnyCall([]{return Make<StubD3D11Device>(); });
        }

        ComPtr<StubD3D11Device> CreateStubD3D11Device() override
        {
            return CreateStubD3D11DeviceMethod.WasCalled();
        }

        virtual bool TryCreateD3DDevice(
            CanvasHardwareAcceleration hardwareAcceleration,
            ComPtr<ID3D11Device>* device) override
        {
            if (m_canCreateDevices)
                return __super::TryCreateD3DDevice(hardwareAcceleration, device);
            else
                return false;
        }

        void SetCreatingDevicesEnabled(bool value)
        {
            m_canCreateDevices = value;
        }

    };

    class Fixture
    {
    public:
        std::shared_ptr<GetSharedDevice_Adapter> Adapter;
        std::shared_ptr<CanvasDeviceManager> Manager;

        Fixture()
            : Adapter(std::make_shared<GetSharedDevice_Adapter>())
            , Manager(std::make_shared<CanvasDeviceManager>(Adapter))
        {
        }
    };

    TEST_METHOD_EX(CanvasGetSharedDeviceTests_NullArg)
    {
        auto canvasDeviceFactory = Make<CanvasDeviceFactory>();

        Assert::AreEqual(E_INVALIDARG, canvasDeviceFactory->GetSharedDevice(CanvasHardwareAcceleration::Auto, nullptr));
    }

    TEST_METHOD_EX(CanvasGetSharedDeviceTests_InvalidArg)
    {
        auto canvasDeviceFactory = Make<CanvasDeviceFactory>();

        ComPtr<ICanvasDevice> unused;
        Assert::AreEqual(E_INVALIDARG, canvasDeviceFactory->GetSharedDevice(CanvasHardwareAcceleration::Unknown, &unused));
        ValidateStoredErrorState(E_INVALIDARG, Strings::GetSharedDeviceUnknown);
    }

    ComPtr<ICanvasDevice> GetSharedDevice_ExpectHardwareAcceleration(
        Fixture& f,
        CanvasHardwareAcceleration passedIn, 
        CanvasHardwareAcceleration expected)
    {
        ComPtr<ICanvasDevice> device = f.Manager->GetSharedDevice(passedIn);

        CanvasHardwareAcceleration retrievedHardwareAcceleration;
        Assert::AreEqual(S_OK, device->get_HardwareAcceleration(&retrievedHardwareAcceleration));
        Assert::AreEqual(expected, retrievedHardwareAcceleration);

        return device;
    }

    TEST_METHOD_EX(CanvasGetSharedDeviceTests_CreateNewDevice)
    {
        Fixture f;

        GetSharedDevice_ExpectHardwareAcceleration(f, CanvasHardwareAcceleration::Auto, CanvasHardwareAcceleration::On);
        GetSharedDevice_ExpectHardwareAcceleration(f, CanvasHardwareAcceleration::On, CanvasHardwareAcceleration::On);
        GetSharedDevice_ExpectHardwareAcceleration(f, CanvasHardwareAcceleration::Off, CanvasHardwareAcceleration::Off);
    }

    TEST_METHOD_EX(CanvasGetSharedDeviceTests_GetExistingDevice)
    {
        Fixture f; 

        // Set up this way to validate against cache entries overwriting the wrong spot.
        ComPtr<ICanvasDevice> devices[_countof(allHardwareAccelerationTypes) * 2];

        devices[0] = GetSharedDevice_ExpectHardwareAcceleration(f, CanvasHardwareAcceleration::Auto, CanvasHardwareAcceleration::On);
        devices[1] = GetSharedDevice_ExpectHardwareAcceleration(f, CanvasHardwareAcceleration::On, CanvasHardwareAcceleration::On);
        devices[2] = GetSharedDevice_ExpectHardwareAcceleration(f, CanvasHardwareAcceleration::Off, CanvasHardwareAcceleration::Off);

        devices[3] = GetSharedDevice_ExpectHardwareAcceleration(f, CanvasHardwareAcceleration::Auto, CanvasHardwareAcceleration::On);
        devices[4] = GetSharedDevice_ExpectHardwareAcceleration(f, CanvasHardwareAcceleration::On, CanvasHardwareAcceleration::On);
        devices[5] = GetSharedDevice_ExpectHardwareAcceleration(f, CanvasHardwareAcceleration::Off, CanvasHardwareAcceleration::Off);

        Assert::AreEqual(devices[0].Get(), devices[3].Get());
        Assert::AreEqual(devices[1].Get(), devices[4].Get());
        Assert::AreEqual(devices[2].Get(), devices[5].Get());
    }

    TEST_METHOD_EX(CanvasGetSharedDeviceTests_CreateNewDevice_Auto_CausesFallback)
    {
        Fixture f;
        f.Adapter->SetHardwareEnabled(false);

        GetSharedDevice_ExpectHardwareAcceleration(f, CanvasHardwareAcceleration::Auto, CanvasHardwareAcceleration::Off);
    }

    TEST_METHOD_EX(CanvasGetSharedDeviceTests_NoDeviceAvailable)
    {
        Fixture f;
        f.Adapter->SetCreatingDevicesEnabled(false);

        ExpectHResultException(E_FAIL, [&]{ f.Manager->GetSharedDevice(CanvasHardwareAcceleration::Auto); });
    }

    TEST_METHOD_EX(CanvasGetSharedDeviceTests_ExistingDevice_Lost_RaisesEvent)
    {
        Fixture f;

        auto d3dDevice = Make<StubD3D11Device>();
        f.Adapter->CreateStubD3D11DeviceMethod.AllowAnyCall([&](){ return d3dDevice; });

        auto device = f.Manager->GetSharedDevice(CanvasHardwareAcceleration::Auto);

        // 
        //Expect the DeviceLost event to get raised.
        //
        MockEventHandler<DeviceLostHandlerType> deviceLostHandler(L"DeviceLost");
        deviceLostHandler.SetExpectedCalls(1);
        EventRegistrationToken token{};
        Assert::AreEqual(S_OK, device->add_DeviceLost(deviceLostHandler.Get(), &token));

        // Lose the device
        int callIndex = 0;
        d3dDevice->GetDeviceRemovedReasonMethod.AllowAnyCall(
            [&] 
            { 
                callIndex++;
                return callIndex == 1? DXGI_ERROR_DEVICE_REMOVED : S_OK;
            });

        // Try and get the cached device again
        f.Manager->GetSharedDevice(CanvasHardwareAcceleration::Auto);
    }

    TEST_METHOD_EX(CanvasGetSharedDeviceTests_ExistingDevice_LastDeviceReferenceWasReleased)
    {
        Fixture f;

        auto device = f.Manager->GetSharedDevice(CanvasHardwareAcceleration::Auto);
        Assert::IsNotNull(device.Get());
        device.Reset();

        auto device2 = f.Manager->GetSharedDevice(CanvasHardwareAcceleration::Auto);
        Assert::IsNotNull(device2.Get());
    }

    TEST_METHOD_EX(CanvasGetSharedDeviceTests_ManagerReleasesAllReferences)
    {
        WeakRef weakDevices[3];
        {
            Fixture f;

            auto d1 = f.Manager->GetSharedDevice(CanvasHardwareAcceleration::Auto);
            ThrowIfFailed(AsWeak(d1.Get(), &weakDevices[0]));

            auto d2 = f.Manager->GetSharedDevice(CanvasHardwareAcceleration::On);
            ThrowIfFailed(AsWeak(d2.Get(), &weakDevices[1]));

            auto d3 = f.Manager->GetSharedDevice(CanvasHardwareAcceleration::Off);
            ThrowIfFailed(AsWeak(d3.Get(), &weakDevices[2]));

            Assert::IsTrue(IsWeakRefValid(weakDevices[0]));
            Assert::IsTrue(IsWeakRefValid(weakDevices[1]));
            Assert::IsTrue(IsWeakRefValid(weakDevices[2]));
        }
        Assert::IsFalse(IsWeakRefValid(weakDevices[0]));
        Assert::IsFalse(IsWeakRefValid(weakDevices[1]));
        Assert::IsFalse(IsWeakRefValid(weakDevices[2]));
    }
};