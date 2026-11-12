#pragma once
#include <QFile>
#include <QtGlobal>
#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dxgi1_4.h>
#include <psapi.h>
#include <windows.h>
#include <wrl/client.h>
#elif defined(Q_OS_LINUX)
#include <unistd.h>
#endif
namespace af {
struct GpuMemory {
    quint64 usage = 0, budget = 0;
    bool available = false;
};
inline GpuMemory gpuMemory(const QString &renderer) {
    GpuMemory result;
#if defined(Q_OS_WIN)
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()))))
        return result;
    for (UINT i = 0;; ++i) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, adapter.GetAddressOf()) == DXGI_ERROR_NOT_FOUND)
            break;
        if (!adapter)
            break;
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)))
            continue;
        // Do not attribute a different adapter's usage to this OpenGL viewport.
        if (!renderer.contains(QString::fromWCharArray(description.Description), Qt::CaseInsensitive))
            continue;
        Microsoft::WRL::ComPtr<IDXGIAdapter3> modern;
        if (FAILED(adapter.As(&modern)))
            continue;
        DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
        if (SUCCEEDED(modern->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory))) {
            result.usage = memory.CurrentUsage;
            result.budget = memory.Budget;
            result.available = true;
            return result;
        }
    }
#else
    Q_UNUSED(renderer);
#endif
    return result;
}
struct ProcessMemory {
    quint64 resident = 0, privateBytes = 0;
    bool available = false, privateAvailable = false;
};
inline ProcessMemory processMemory() {
    ProcessMemory r;
#if defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS_EX c{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&c),
                             sizeof(c))) {
        r.resident = c.WorkingSetSize;
        r.privateBytes = c.PrivateUsage;
        r.available = r.privateAvailable = true;
    }
#elif defined(Q_OS_LINUX)
    QFile f("/proc/self/statm");
    if (f.open(QIODevice::ReadOnly)) {
        const auto fields = f.readAll().simplified().split(' ');
        bool ok = false;
        const auto pages = fields.value(1).toULongLong(&ok);
        const auto size = sysconf(_SC_PAGESIZE);
        if (ok && size > 0) {
            r.resident = pages * quint64(size);
            r.available = true;
        }
    }
#endif
    return r;
}
} // namespace af
