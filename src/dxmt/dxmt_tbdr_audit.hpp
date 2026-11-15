#pragma once
// Opt-in diagnostics only: never inserts queries, encoders, flushes or waits.
#include "util_env.hpp"
#include "log/log.hpp"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
namespace dxmt {
inline bool tbdrAuditEnabled(){static const bool value=env::getEnvVar("DXMT_TBDR_AUDIT")=="1";return value;}
inline bool tbdrAuditDetail(){static const bool value=env::getEnvVar("DXMT_TBDR_AUDIT_DETAIL")=="1";return value;}
inline uint64_t tbdrAuditNumber(const char *name,uint64_t fallback){
 auto text=env::getEnvVar(name);if(text.empty())return fallback;
 char *end=nullptr;auto value=std::strtoull(text.c_str(),&end,10);return end && !*end ? value:fallback;
}
inline bool tbdrAuditSelected(uint64_t frame,uint64_t seq){
 static const auto start=tbdrAuditNumber("DXMT_TBDR_AUDIT_START_FRAME",0);
 static const auto frames=tbdrAuditNumber("DXMT_TBDR_AUDIT_FRAME_COUNT",0);
 static const auto limit=tbdrAuditNumber("DXMT_TBDR_AUDIT_CB_LIMIT",50000);
 return tbdrAuditEnabled() && seq<=limit && frame>=start && (!frames || frame-start<frames);
}
inline void tbdrAuditLog(const char *format,...){
 char row[2048];va_list args;va_start(args,format);std::vsnprintf(row,sizeof(row),format,args);va_end(args);
 Logger::info(std::string("TBDR ")+row);
}
inline bool tbdrAuditDetailRow(){
 if(!tbdrAuditEnabled() || !tbdrAuditDetail())return false;
 static std::atomic<unsigned> count{0};
 return count.fetch_add(1,std::memory_order_relaxed)<20000;
}
struct TBDRAuditStats {
 bool active=false;
 uint64_t render_before=0,clear_before=0,resolve_before=0;
 uint64_t mapped_render=0;
 uint64_t read_ordinary=0,read_capable=0;
 uint64_t render_after=0,clear_after=0,resolve_after=0,compute=0,blit=0,temporal=0,present=0;
 uint64_t signals=0,waits=0,timestamps=0,fence_waits=0;
 uint64_t load_proxy=0,store_proxy=0,resolve_proxy=0,shaderwrite_proxy=0,pfv_proxy=0,unknown_formats=0;
 uint64_t merge_map=0,merge_layout=0,merge_argbuf=0,merge_attachment=0,merge_action=0,merge_dependency=0;
 uint64_t blit_ops=0,texture_copies=0,buffer_copies=0;
 double prepare_ms=0,encode_ms=0;
};
}
