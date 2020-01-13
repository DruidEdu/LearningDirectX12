#include <DX12LibPCH.h>

#include <Resource.h>

#include <Device.h>
#include <ResourceStateTracker.h>

Resource::Resource(const std::wstring& name)
: m_Device(nullptr)
, m_d3d12Resource(nullptr)
, m_FormatSupport({})
, m_d3d12ClearValue(nullptr)
, m_ResourceName(name)
{}

Resource::Resource(std::shared_ptr<Device> device, const std::wstring& name)
    : m_Device(device)
    , m_ResourceName(name)
    , m_FormatSupport({})
{}

Resource::Resource(std::shared_ptr<Device> device, const D3D12_RESOURCE_DESC& resourceDesc, const D3D12_CLEAR_VALUE* clearValue, const std::wstring& name)
    : m_Device(device)
{
    if ( clearValue )
    {
        m_d3d12ClearValue = std::make_unique<D3D12_CLEAR_VALUE>(*clearValue);
    }
    
    auto d3d12device = m_Device->GetD3D12Device();

    ThrowIfFailed(d3d12device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        m_d3d12ClearValue.get(),
        IID_PPV_ARGS(&m_d3d12Resource)
    ) );

    ResourceStateTracker::AddGlobalResourceState(m_d3d12Resource.Get(), D3D12_RESOURCE_STATE_COMMON );

    CheckFeatureSupport();
    SetName(name);
}

Resource::Resource(std::shared_ptr<Device> device, Microsoft::WRL::ComPtr<CD3DX12AffinityResource> resource, const std::wstring& name)
    : m_Device(device)
    , m_d3d12Resource(resource)
    , m_FormatSupport({})
{
    CheckFeatureSupport();
    SetName(name);
}

Resource::Resource(const Resource& copy)
    : m_Device(copy.m_Device)
    , m_d3d12Resource(copy.m_d3d12Resource)
    , m_FormatSupport(copy.m_FormatSupport)
    , m_ResourceName(copy.m_ResourceName)
    , m_d3d12ClearValue(std::make_unique<D3D12_CLEAR_VALUE>(*copy.m_d3d12ClearValue))
{}

Resource::Resource(Resource&& copy) noexcept
    : m_Device(std::move(copy.m_Device))
    , m_d3d12Resource(std::move(copy.m_d3d12Resource))
    , m_FormatSupport(copy.m_FormatSupport)
    , m_ResourceName(std::move(copy.m_ResourceName))
    , m_d3d12ClearValue(std::move(copy.m_d3d12ClearValue))
{}

Resource& Resource::operator=(const Resource& other)
{
    if ( this != &other )
    {
        m_Device = other.m_Device;
        m_d3d12Resource = other.m_d3d12Resource;
        m_FormatSupport = other.m_FormatSupport;
        m_ResourceName = other.m_ResourceName;
        if ( other.m_d3d12ClearValue )
        {
            m_d3d12ClearValue = std::make_unique<D3D12_CLEAR_VALUE>( *other.m_d3d12ClearValue );
        }
    }

    return *this;
}

Resource& Resource::operator=(Resource&& other) noexcept
{
    if (this != &other)
    {
        m_Device = std::move(other.m_Device);
        m_d3d12Resource = std::move(other.m_d3d12Resource);
        m_FormatSupport = other.m_FormatSupport;
        m_ResourceName = std::move(other.m_ResourceName);
        m_d3d12ClearValue = std::move( other.m_d3d12ClearValue );

        other.Reset();
    }

    return *this;
}


Resource::~Resource()
{
}

void Resource::SetD3D12Resource(std::shared_ptr<Device> device,
    Microsoft::WRL::ComPtr<CD3DX12AffinityResource> d3d12Resource, 
    const D3D12_CLEAR_VALUE* clearValue)
{
    m_Device = device;
    m_d3d12Resource = d3d12Resource;

    if ( m_d3d12ClearValue )
    {
        m_d3d12ClearValue = std::make_unique<D3D12_CLEAR_VALUE>( *clearValue );
    }
    else
    {
        m_d3d12ClearValue.reset();
    }
    CheckFeatureSupport();
    SetName(m_ResourceName);
}

void Resource::SetName(const std::wstring& name)
{
    m_ResourceName = name;
    if (m_d3d12Resource && !m_ResourceName.empty())
    {
        m_d3d12Resource->SetName(m_ResourceName.c_str());
    }
}

void Resource::Reset()
{
    m_d3d12Resource.Reset();
    m_FormatSupport = {};
    m_d3d12ClearValue.reset();
    m_ResourceName.clear();
}

bool Resource::CheckFormatSupport(D3D12_FORMAT_SUPPORT1 formatSupport) const
{
    return (m_FormatSupport.Support1 & formatSupport) != 0;
}

bool Resource::CheckFormatSupport(D3D12_FORMAT_SUPPORT2 formatSupport) const
{
    return (m_FormatSupport.Support2 & formatSupport) != 0;
}

void Resource::CheckFeatureSupport()
{
    if (m_d3d12Resource)
    {
        auto desc = m_d3d12Resource->GetDesc();
        auto d3d12Device = m_Device->GetD3D12Device();

        m_FormatSupport.Format = desc.Format;
        ThrowIfFailed(d3d12Device->CheckFeatureSupport(
            D3D12_FEATURE_FORMAT_SUPPORT,
            &m_FormatSupport,
            sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT)));
    }
    else
    {
        m_FormatSupport = {};
    }
}
