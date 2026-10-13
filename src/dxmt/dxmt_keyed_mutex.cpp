/*
 * Copyright 2026 Feifan He for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "dxmt_keyed_mutex.hpp"
#include "log/log.hpp"
#include <random>
#include <chrono>

namespace dxmt {

void
KeyedMutex::incRef() {
  refcount_.fetch_add(1u, std::memory_order_acquire);
};

void
KeyedMutex::decRef() {
  if (refcount_.fetch_sub(1u, std::memory_order_release) == 1u)
    delete this;
};

#ifdef _WIN32

KeyedMutex::~KeyedMutex() {
  if (owner_thread_.joinable()) {
    {
      std::unique_lock<dxmt::mutex> lock(owner_lock_);
      owner_stop_ = true;
    }
    owner_cond_.notify_all();
    owner_thread_.join();
  }
  if (local_kmt_) {
    D3DKMT_DESTROYKEYEDMUTEX destroy = {};
    destroy.hKeyedMutex = local_kmt_;
    D3DKMTDestroyKeyedMutex(&destroy);
    local_kmt_ = {};
  }
  global_kmt_ = {};
};

void
makeUniqueName(char (&out)[54]) {
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> dis;

  uint64_t r1 = dis(gen);
  uint64_t r2 = dis(gen);

  std::snprintf(
      out, 54, "DXMT_shared_resource_%016llx%016llx", static_cast<unsigned long long>(r1),
      static_cast<unsigned long long>(r2)
  );
}

Rc<KeyedMutex>
KeyedMutex::create(WMT::Device device) {
  D3DKMT_CREATEKEYEDMUTEX2 create = {};
  create.Flags.NtSecuritySharing = false;
  create.InitialValue = 0;

  auto event = device.newSharedEvent();

  mach_port_t mach_port = event.createMachPort();
  if (!mach_port) {
    ERR("KeyedMutex: Failed to create mach port.");
    return {};
  }
  char mach_port_name[54];
  makeUniqueName(mach_port_name);
  if (!WMTBootstrapRegister(mach_port_name, mach_port)) {
    ERR("KeyedMutex: Failed to register mach port.");
    return {};
  }
  create.pPrivateRuntimeData = mach_port_name;
  create.PrivateRuntimeDataSize = sizeof(mach_port_name);

  if (D3DKMTCreateKeyedMutex2(&create)) {
    ERR("KeyedMutex: Failed to create keyed mutex.");
    return {};
  }

  auto ret = Rc(new KeyedMutex());
  ret->global_kmt_ = create.hSharedHandle;
  ret->local_kmt_ = create.hKeyedMutex;
  ret->event_ = std::move(event);

  return ret;
}

Rc<KeyedMutex>
KeyedMutex::import(WMT::Device device, D3DKMT_HANDLE global_kmt) {
  char mach_port_name[54];
  D3DKMT_OPENKEYEDMUTEX2 open = {};
  open.hSharedHandle = global_kmt;
  open.pPrivateRuntimeData = mach_port_name;
  open.PrivateRuntimeDataSize = sizeof(mach_port_name);

  if (D3DKMTOpenKeyedMutex2(&open)) {
    ERR("KeyedMutex: Failed to open keyed mutex.");
    return {};
  }

  mach_port_t mach_port;
  if (!WMTBootstrapLookUp(mach_port_name, &mach_port)) {
    ERR("KeyedMutex: Failed to lookup mach port.");
    return {};
  }

  auto ret = Rc(new KeyedMutex());
  ret->global_kmt_ = global_kmt;
  ret->local_kmt_ = open.hKeyedMutex;
  ret->event_ = device.newSharedEventWithMachPort(mach_port);
  return ret;
}

NTSTATUS
KeyedMutex::runOnOwnerThread(const std::function<NTSTATUS()> &call, const char *operation, uint64_t key,
                            uint32_t timeout_ms) {
  using Clock = std::chrono::steady_clock;
  auto started = Clock::now();
  std::unique_lock<dxmt::mutex> serial(call_lock_);
  auto serialized = Clock::now();
  std::unique_lock<dxmt::mutex> lock(owner_lock_);
  if (!owner_thread_.joinable()) {
    owner_thread_ = dxmt::thread([this] {
      std::unique_lock<dxmt::mutex> lock(owner_lock_);
      while (true) {
        owner_cond_.wait(lock, [this] { return owner_stop_ || owner_call_ != nullptr; });
        if (owner_stop_)
          return;
        const std::function<NTSTATUS()> *call = owner_call_;
        owner_call_ = nullptr;
        lock.unlock();
        NTSTATUS result = (*call)();
        lock.lock();
        owner_result_ = result;
        owner_done_ = true;
        owner_cond_.notify_all();
      }
    });
  }
  Clock::time_point entered, finished;
  std::function<NTSTATUS()> measuredCall = [&] {
    entered = Clock::now();
    NTSTATUS result = call();
    finished = Clock::now();
    return result;
  };
  owner_done_ = false;
  auto queued = Clock::now();
  owner_call_ = &measuredCall;
  owner_cond_.notify_all();
  owner_cond_.wait(lock, [this] { return owner_done_; });
  auto returned = Clock::now();
  NTSTATUS result = owner_result_;
  lock.unlock();
  serial.unlock();
  auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
  if (ms(started, returned) >= 10.0 || result) {
    static std::atomic<uint64_t> slow_calls[2]{};
    uint64_t count = ++slow_calls[operation[0] == 'a' ? 0 : 1];
    if (count <= 8 || count % 128 == 0) {
      WARN("KeyedMutex owner phases: tick_ms=", GetTickCount64(), " pid=", GetCurrentProcessId(), " caller=", GetCurrentThreadId(),
           " handle=", global_kmt_, " op=", operation, " key=", key, " timeout_ms=", timeout_ms,
           " status=", result, " serial_ms=", ms(started, serialized),
           " dispatch_ms=", ms(serialized, queued), " wake_ms=", ms(queued, entered),
           " wine_ms=", ms(entered, finished), " return_ms=", ms(finished, returned), " count=", count);
    }
  }
  return result;
}

HRESULT
KeyedMutex::acquire(KeyedMutexAcquire &&func_acquire, uint64_t key, uint32_t milliseconds) {
  /**
   * doc: If the owning device attempted to create another keyed mutex on the same shared resource, AcquireSync returns
   * E_FAIL.
   */
  if (owned_.load(std::memory_order_acquire))
    return E_FAIL;

  // NT timeouts are relative when negative and absolute when positive; a
  // null timeout waits forever.
  LARGE_INTEGER timeout = {};
  D3DKMT_ACQUIREKEYEDMUTEX acquire = {};
  acquire.hKeyedMutex = local_kmt_;
  acquire.Key = key;
  if (milliseconds != INFINITE) {
    timeout.QuadPart = -static_cast<int64_t>(milliseconds) * 10000;
    acquire.pTimeout = &timeout;
  }

  NTSTATUS status = runOnOwnerThread([&acquire] { return D3DKMTAcquireKeyedMutex(&acquire); }, "acquire", key, milliseconds);
  if (status == STATUS_TIMEOUT)
    return WAIT_TIMEOUT;
  if (status == STATUS_ABANDONED_WAIT_0)
    return WAIT_ABANDONED;
  if (status) {
    WARN("KeyedMutex: Failed to acquire keyed mutex.");
    return E_FAIL;
  }

  ownership_granted_ = std::chrono::steady_clock::now();
  ownership_tick_ms_ = GetTickCount64();
  func_acquire(acquire.FenceValue);
  ownership_ready_ = std::chrono::steady_clock::now();

  fence_value_ = acquire.FenceValue;
  owned_.store(true, std::memory_order_release);
  return S_OK;
}

HRESULT
KeyedMutex::release(KeyedMutexRelease &&func_release, uint64_t key) {
  /**
   * doc: If the device attempted to release a keyed mutex that is not valid or owned by the device, ReleaseSync returns
   * E_FAIL. */
  if (!owned_.load(std::memory_order_acquire))
    return E_FAIL;

  auto release_entered = std::chrono::steady_clock::now();
  func_release(fence_value_ + 1);
  auto signal_queued = std::chrono::steady_clock::now();

  D3DKMT_RELEASEKEYEDMUTEX release = {};
  release.hKeyedMutex = local_kmt_;
  release.Key = key;
  release.FenceValue = fence_value_ + 1;

  if (runOnOwnerThread([&release] { return D3DKMTReleaseKeyedMutex(&release); }, "release", key, 0)) {
    WARN("KeyedMutex: Failed to release keyed mutex.");
    return E_FAIL;
  }

  auto finished = std::chrono::steady_clock::now();
  owned_.store(false, std::memory_order_release);
  auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
  if (ms(ownership_granted_, ownership_ready_) >= 10.0 ||
      ms(ownership_ready_, release_entered) >= 10.0 || ms(release_entered, finished) >= 10.0) {
    static std::atomic<uint64_t> long_ownerships{0};
    uint64_t count = ++long_ownerships;
    if (count <= 8 || count % 128 == 0) {
      WARN("KeyedMutex ownership interval: pid=", GetCurrentProcessId(), " caller=", GetCurrentThreadId(),
           " handle=", global_kmt_, " acquired_tick_ms=", ownership_tick_ms_, " released_tick_ms=", GetTickCount64(),
           " release_fence=", fence_value_ + 1,
           " acquire_setup_ms=", ms(ownership_granted_, ownership_ready_),
           " app_owned_ms=", ms(ownership_ready_, release_entered),
           " signal_enqueue_ms=", ms(release_entered, signal_queued),
           " release_handoff_ms=", ms(signal_queued, finished), " count=", count);
    }
  }
  return S_OK;
}

#else

KeyedMutex::~KeyedMutex() {};

Rc<KeyedMutex>
KeyedMutex::create(WMT::Device device) {
  return {};
}

Rc<KeyedMutex>
KeyedMutex::import(WMT::Device device, D3DKMT_HANDLE global_kmt) {
  return {};
}

HRESULT
KeyedMutex::acquire(KeyedMutexAcquire &&func_acquire, uint64_t key, uint32_t milliseconds) {
  return DXGI_ERROR_INVALID_CALL;
}

HRESULT
KeyedMutex::release(KeyedMutexRelease &&func_release, uint64_t key) {
  return DXGI_ERROR_INVALID_CALL;
}

#endif

}; // namespace dxmt