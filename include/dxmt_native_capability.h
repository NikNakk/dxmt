/* Copyright 2026 Nick Kennedy
 * SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

/* Native macOS boundary only. Windows consumers use IDXMTNativeDevice.
 * Bootstrap names identify revocable broker receive rights, never raw Metal
 * resource ports. Destroying the broker invalidates future imports while
 * already imported textures/events retain their own native capabilities. */
#include <mach/message.h>
#include <stdint.h>

#define DXMT_NATIVE_CAP_VERSION 1u
#define DXMT_NATIVE_CAP_REQUEST_ID 0x44584d43
#define DXMT_NATIVE_CAP_REPLY_ID 0x44584d44
#define DXMT_NATIVE_CAP_TEXTURE 1u
#define DXMT_NATIVE_CAP_EVENT 2u

struct dxmt_native_cap_request {
  mach_msg_header_t header;
  uint32_t version;
  uint32_t kind;
};

struct dxmt_native_cap_reply {
  mach_msg_header_t header;
  mach_msg_body_t body;
  mach_msg_port_descriptor_t resource;
  uint32_t version;
  uint32_t kind;
};
