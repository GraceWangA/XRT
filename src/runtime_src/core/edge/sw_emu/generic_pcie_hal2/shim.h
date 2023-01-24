// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2016-2022 Xilinx, Inc. All rights reserved.
// Copyright (C) 2022 Advanced Micro Devices, Inc. All rights reserved.
#ifndef _SW_EMU_SHIM_H_
#define _SW_EMU_SHIM_H_

#include "unix_socket.h"
#include "config.h"
#include "em_defines.h"
#include "memorymanager.h"
#include "rpc_messages.pb.h"

#include "xcl_api_macros.h"
#include "xcl_macros.h"
#include "xclbin.h"
#include "core/common/device.h"
#include "core/common/scheduler.h"
#include "core/common/message.h"
#include "core/common/xrt_profiling.h"
#include "core/common/api/xclbin_int.h"
#include "core/common/shim/hwctx_handle.h"
#include "core/include/experimental/xrt_xclbin.h"
#include "core/include/xdp/common.h"
#include "core/include/xdp/counters.h"
#include "core/include/xdp/trace.h"
#include "swscheduler.h"

#include <stdarg.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <thread>
#include <tuple>
#include <sys/wait.h>
#ifndef _WINDOWS
#include <dlfcn.h>
#endif

namespace xclswemuhal2 {
  // XDMA Shim
  class SwEmuShim {
    public:
      static const unsigned TAG;
      static const unsigned CONTROL_AP_START;
      static const unsigned CONTROL_AP_DONE;
      static const unsigned CONTROL_AP_IDLE;
      static const unsigned CONTROL_AP_CONTINUE;

    // Shim handle for hardware context. Even as sw_emu does not
    // support hardware context, it still must implement a shim
    // hardware context handle representing the default slot
    class hwcontext : public xrt_core::hwctx_handle
    {
      SwEmuShim* m_shim;
      xrt::uuid m_uuid;
      slot_id m_slotidx;
      xrt::hw_context::access_mode m_mode;

    public:
      hwcontext(SwEmuShim* shim, slot_id slotidx, xrt::uuid uuid, xrt::hw_context::access_mode mode)
        : m_shim(shim)
        , m_uuid(std::move(uuid))
        , m_slotidx(slotidx)
        , m_mode(mode)
      {}

      slot_id
      get_slotidx() const override
      {
        return m_slotidx;
      }

      xrt::hw_context::access_mode
      get_mode() const
      {
        return m_mode;
      }

      xrt::uuid
      get_xclbin_uuid() const
      {
        return m_uuid;
      }

      std::unique_ptr<xrt_core::hwqueue_handle>
      create_hw_queue() override
      {
        return nullptr;
      }

      xrt_buffer_handle // tobe: std::unique_ptr<buffer_handle>
      alloc_bo(void* userptr, size_t size, unsigned int flags) override
      {
        // The hwctx is embedded in the flags, use regular shim path
        auto bo = m_shim->xclAllocUserPtrBO(userptr, size, flags);
        if (bo == XRT_NULL_BO)
          throw std::bad_alloc();

        return to_xrt_buffer_handle(bo);
      }

      xrt_buffer_handle // tobe: std::unique_ptr<buffer_handle>
      alloc_bo(size_t size, unsigned int flags) override
      {
        // The hwctx is embedded in the flags, use regular shim path
        auto bo = m_shim->xclAllocBO(size, flags);
        if (bo == XRT_NULL_BO)
          throw std::bad_alloc();

        return to_xrt_buffer_handle(bo);
      }

      xrt_core::cuidx_type
      open_cu_context(const std::string& cuname) override
      {
        return m_shim->open_cu_context(this, cuname);
      }

      void
      close_cu_context(xrt_core::cuidx_type cuidx) override
      {
        m_shim->close_cu_context(this, cuidx);
      }

      void
      exec_buf(xrt_buffer_handle cmd) override
      {
        m_shim->xclExecBuf(to_xclBufferHandle(cmd));
      }
    }; // class hwcontext
  private:
      // This is a hidden signature of this class and helps in preventing
      // user errors when incorrect pointers are passed in as handles.
      const unsigned mTag;

  private:
      // Helper functions - added for kernel debug
      int dumpXML(const xclBin* header, std::string& fileLocation) ;
      bool isAieEnabled(const xclBin* header);
      bool parseIni(unsigned int& debugPort) ;
      static std::map<std::string, std::string> mEnvironmentNameValueMap;
      void getCuRangeIdx();
  public:
      // HAL2 RELATED member functions start
      unsigned int xclAllocBO(size_t size, unsigned flags);
      uint64_t xoclCreateBo(xclemulation::xocl_create_bo *info);
      void* xclMapBO(unsigned int boHandle, bool write);
      int xclUnmapBO(unsigned int boHandle, void* addr);
      int xclSyncBO(unsigned int boHandle, xclBOSyncDirection dir, size_t size, size_t offset);
      unsigned int xclAllocUserPtrBO(void *userptr, size_t size, unsigned flags);
      int xclGetBOProperties(unsigned int boHandle, xclBOProperties *properties);
      size_t xclWriteBO(unsigned int boHandle, const void *src, size_t size, size_t seek);
      size_t xclReadBO(unsigned int boHandle, void *dst, size_t size, size_t skip);
      void xclFreeBO(unsigned int boHandle);
      //P2P buffer support
      int xclExportBO(unsigned int boHandle);
      unsigned int xclImportBO(int boGlobalHandle, unsigned flags);
      int xclCopyBO(unsigned int dst_boHandle, unsigned int src_boHandle, size_t size, size_t dst_offset, size_t src_offset);
      static int xclLogMsg(xclDeviceHandle handle, xrtLogMsgLevel level, const char* tag, const char* format, va_list args1);


      xclemulation::drm_xocl_bo* xclGetBoByHandle(unsigned int boHandle);
      inline unsigned short xocl_ddr_channel_count();
      inline unsigned long long xocl_ddr_channel_size();
      // HAL2 RELATED member functions end

      //Configuration
      void socketConnection(bool isTCPSocket);
      void setDriverVersion(const std::string& version);
      void xclOpen(const char* logfileName);
      int xclLoadXclBin(const xclBin *buffer);
      int xclLoadXclBinNewFlow(const xclBin *buffer);
      //int xclLoadBitstream(const char *fileName);
      int xclUpgradeFirmware(const char *fileName);
      int xclBootFPGA();
      void xclClose();
      void resetProgram(bool callingFromClose = false);

      // Raw read/write
      size_t xclWrite(xclAddressSpace space, uint64_t offset, const void *hostBuf, size_t size);
      size_t xclRead(xclAddressSpace space, uint64_t offset, void *hostBuf, size_t size);

      // Buffer management
      uint64_t xclAllocDeviceBuffer(size_t size);
      uint64_t xclAllocDeviceBuffer2(size_t& size, xclMemoryDomains domain, unsigned flags, bool p2pBuffer, std::string &sFileName);

      void xclFreeDeviceBuffer(uint64_t buf);
      size_t xclCopyBufferHost2Device(uint64_t dest, const void *src, size_t size, size_t seek);
      size_t xclCopyBufferDevice2Host(void *dest, uint64_t src, size_t size, size_t skip);

      // Performance monitoring
      // Control
      double xclGetDeviceClockFreqMHz();
      double xclGetHostReadMaxBandwidthMBps();
      double xclGetHostWriteMaxBandwidthMBps();
      double xclGetKernelReadMaxBandwidthMBps();
      double xclGetKemrnelWriteMaxBandwidthMBps();
      void xclSetProfilingNumberSlots(xdp::MonitorType type, uint32_t numSlots);
      size_t xclPerfMonClockTraining(xdp::MonitorType type);
      // Counters
      size_t xclPerfMonStartCounters(xdp::MonitorType type);
      size_t xclPerfMonStopCounters(xdp::MonitorType type);
      size_t xclPerfMonReadCounters(xdp::MonitorType type, xdp::CounterResults& counterResults);
      // Trace
      size_t xclPerfMonStartTrace(xdp::MonitorType type, uint32_t startTrigger);
      size_t xclPerfMonStopTrace(xdp::MonitorType type);
      uint32_t xclPerfMonGetTraceCount(xdp::MonitorType type);
      size_t xclPerfMonReadTrace(xdp::MonitorType type, xdp::TraceEventsVector& traceVector);

      // Sanity checks
      int xclGetDeviceInfo2(xclDeviceInfo2 *info);
      static unsigned xclProbe();
      void fillDeviceInfo(xclDeviceInfo2* dest, xclDeviceInfo2* src);
      void saveDeviceProcessOutput();

      void set_messagesize( unsigned int messageSize ) { message_size = messageSize; }
      unsigned int get_messagesize(){ return message_size; }


      ~SwEmuShim();
      SwEmuShim(unsigned int deviceIndex, xclDeviceInfo2 &info, std::list<xclemulation::DDRBank>& DDRBankList, bool bUnified, bool bXPR, FeatureRomHeader &featureRom );

      static SwEmuShim *handleCheck(void *handle);
      bool isGood() const;

      int xclOpenContext(const uuid_t xclbinId, unsigned int ipIndex, bool shared);
      int xclExecWait(int timeoutMilliSec);
      int xclExecBuf(unsigned int cmdBO);
      int xclCloseContext(const uuid_t xclbinId, unsigned int ipIndex);
      //Get CU index from IP_LAYOUT section for corresponding kernel name
      int xclIPName2Index(const char *name);
      //Check if its a valid CU by comparing with sorted cu list
      bool isValidCu(uint32_t cu_index);
      //get Address Range for a particular cu from mCURangeMap
      uint64_t getCuAddRange(uint32_t cu_index);
      //Check if the offset is valid and within the cuAddRange limit
      bool isValidOffset(uint32_t offset, uint64_t cuAddRange);
      //common proc which calls xclRegRead/xclRegWrite RPC calls based on read/write
      int xclRegRW(bool rd, uint32_t cu_index, uint32_t offset, uint32_t *datap);
      int xclRegRead(uint32_t cu_index, uint32_t offset, uint32_t *datap);
      int xclRegWrite(uint32_t cu_index, uint32_t offset, uint32_t data);
      bool isImported(unsigned int _bo)
      {
        if (mImportedBOs.find(_bo) != mImportedBOs.end())
          return true;
        return false;
      }
      struct exec_core* getExecCore() { return mCore; }
      SWScheduler* getScheduler() { return mSWSch; }
      //******************************* XRT Graph API's **************************************************//
      /**
      * xrtGraphInit() - Initialize graph
      *
      * @gh:             Handle to graph previously opened with xrtGraphOpen.
      * Return:          0 on success, -1 on error
      *
      * Note: Run by enable tiles and disable tile reset
      */
      int
        xrtGraphInit(void * gh);

      /**
      * xrtGraphRun() - Start a graph execution
      *
      * @gh:             Handle to graph previously opened with xrtGraphOpen.
      * @iterations:     The run iteration to update to graph. 0 for infinite.
      * Return:          0 on success, -1 on error
      *
      * Note: Run by enable tiles and disable tile reset
      */
      int
        xrtGraphRun(void * gh, uint32_t iterations);

      /**
      * xrtGraphWait() -  Wait a given AIE cycle since the last xrtGraphRun and
      *                   then stop the graph. If cycle is 0, busy wait until graph
      *                   is done. If graph already run more than the given
      *                   cycle, stop the graph immediateley.
      *
      * @gh:              Handle to graph previously opened with xrtGraphOpen.
      *
      * Return:          0 on success, -1 on error.
      *
      * Note: This API with non-zero AIE cycle is for graph that is running
      * forever or graph that has multi-rate core(s).
      */
      int
        xrtGraphWait(void * gh);

      /**
      * xrtGraphEnd() - Wait a given AIE cycle since the last xrtGraphRun and
      *                 then end the graph. busy wait until graph
      *                 is done before end the graph. If graph already run more
      *                 than the given cycle, stop the graph immediately and end it.
      *
      * @gh:              Handle to graph previously opened with xrtGraphOpen.
      *
      * Return:          0 on success, -1 on timeout.
      *
      * Note: This API with non-zero AIE cycle is for graph that is running
      * forever or graph that has multi-rate core(s).
      */
      int
        xrtGraphEnd(void * gh);

      /**
      * xrtGraphUpdateRTP() - Update RTP value of port with hierarchical name
      *
      * @gh:              Handle to graph previously opened with xrtGraphOpen.
      * @hierPathPort:    hierarchial name of RTP port.
      * @buffer:          pointer to the RTP value.
      * @size:            size in bytes of the RTP value.
      *
      * Return:          0 on success, -1 on error.
      */
      int
        xrtGraphUpdateRTP(void * gh, const char *hierPathPort, const char *buffer, size_t size);

      /**
      * xrtGraphUpdateRTP() - Read RTP value of port with hierarchical name
      *
      * @gh:              Handle to graph previously opened with xrtGraphOpen.
      * @hierPathPort:    hierarchial name of RTP port.
      * @buffer:          pointer to the buffer that RTP value is copied to.
      * @size:            size in bytes of the RTP value.
      *
      * Return:          0 on success, -1 on error.
      *
      * Note: Caller is reponsible for allocating enough memory for RTP value
      *       being copied to.
      */
      int
        xrtGraphReadRTP(void * gh, const char *hierPathPort, char *buffer, size_t size);

      /**
      * xrtSyncBOAIENB() - Transfer data between DDR and Shim DMA channel
      *
      * @bo:           BO obj.
      * @gmioName:        GMIO port name
      * @dir:             GM to AIE or AIE to GM
      * @size:            Size of data to synchronize
      * @offset:          Offset within the BO
      *
      * Return:          0 on success, or appropriate error number.
      *
      * Synchronize the buffer contents between GMIO and AIE.
      * Note: Upon return, the synchronization is submitted or error out
      */
      int
        xrtSyncBOAIENB(xrt::bo& bo, const char *gmioname, enum xclBOSyncDirection dir, size_t size, size_t offset);

      /**
      * xrtGMIOWait() - Wait a shim DMA channel to be idle for a given GMIO port
      *
      * @gmioName:        GMIO port name
      *
      * Return:          0 on success, or appropriate error number.
      */
      int
        xrtGMIOWait(const char *gmioname);

      /**
      * xrtGraphResume() - Resume a suspended graph.
      *
      * Resume graph execution which was paused by suspend() or wait(cycles) APIs
      */
      int
        xrtGraphResume(void * gh);

      /**
      * xrtGraphTimedEnd() - Wait a given AIE cycle since the last xrtGraphRun and
      *                 then end the graph. If cycle is 0, busy wait until graph
      *                 is done before end the graph. If graph already run more
      *                 than the given cycle, stop the graph immediately and end it.
      */
      int
        xrtGraphTimedEnd(void * gh, uint64_t cycle);

      /**
      * xrtGraphTimedWait() -  Wait a given AIE cycle since the last xrtGraphRun and
      *                   then stop the graph. If cycle is 0, busy wait until graph
      *                   is done. If graph already run more than the given
      *                   cycle, stop the graph immediateley.
      */
      int
      xrtGraphTimedWait(void * gh, uint64_t cycle);

      ////////////////////////////////////////////////////////////////
      // Internal SHIM APIs
      ////////////////////////////////////////////////////////////////
      xrt_core::cuidx_type
      open_cu_context(const xrt_core::hwctx_handle* hwctx_hdl, const std::string& cuname);

      void
      close_cu_context(const xrt_core::hwctx_handle* hwctx_hdl, xrt_core::cuidx_type cuidx);

      std::unique_ptr<xrt_core::hwctx_handle>
      create_hw_context(const xrt::uuid&, const xrt::hw_context::qos_type&, xrt::hw_context::access_mode);

  private:
      std::shared_ptr<xrt_core::device> mCoreDevice;
      std::mutex mMemManagerMutex;

      // Performance monitoring helper functions
      bool isDSAVersion(double checkVersion, bool onlyThisVersion);
      uint64_t getHostTraceTimeNsec();
      uint64_t getPerfMonBaseAddress(xdp::MonitorType type);
      uint64_t getPerfMonFifoBaseAddress(xdp::MonitorType type, uint32_t fifonum);
      uint64_t getPerfMonFifoReadBaseAddress(xdp::MonitorType type, uint32_t fifonum);
      uint32_t getPerfMonNumberSlots(xdp::MonitorType type);
      uint32_t getPerfMonNumberSamples(xdp::MonitorType type);
      uint32_t getPerfMonNumberFifos(xdp::MonitorType type);
      uint32_t getPerfMonByteScaleFactor(xdp::MonitorType type);
      uint8_t  getPerfMonShowIDS(xdp::MonitorType type);
      uint8_t  getPerfMonShowLEN(xdp::MonitorType type);
      size_t resetFifos(xdp::MonitorType type);
      uint32_t bin2dec(std::string str, int start, int number);
      uint32_t bin2dec(const char * str, int start, int number);
      std::string dec2bin(uint32_t n);
      std::string dec2bin(uint32_t n, unsigned bits);

      std::mutex mtx;
      unsigned int message_size;
      bool simulator_started;

      std::ofstream mLogStream;
      xclVerbosityLevel mVerbosity;

      std::vector<std::string> mTempdlopenfilenames;
      std::string deviceName;
      std::string deviceDirectory;
      std::list<xclemulation::DDRBank> mDdrBanks;
      std::map<uint64_t,std::pair<std::string,unsigned int>> kernelArgsInfo;
      xclDeviceInfo2 mDeviceInfo;

      void launchDeviceProcess(bool debuggable, std::string& binDir);
      void launchTempProcess();
      void initMemoryManager(std::list<xclemulation::DDRBank>& DDRBankList);
      std::vector<xclemulation::MemoryManager *> mDDRMemoryManager;

      void* ci_buf;
      call_packet_info ci_msg;

      response_packet_info ri_msg;
      void* ri_buf;
      size_t alloc_void(size_t new_size);

      void* buf;
      size_t buf_size;
      unsigned int binaryCounter;
      unix_socket* sock;
      unix_socket* aiesim_sock;

      uint64_t mRAMSize;
      size_t mCoalesceThreshold;
      unsigned int mDeviceIndex;
      bool mCloseAll;

      std::mutex mProcessLaunchMtx;
      std::mutex mApiMtx;
      static bool mFirstBinary;
      bool bUnified;
      bool bXPR;
      // HAL2 RELATED member variables start
      std::map<int, xclemulation::drm_xocl_bo*> mXoclObjMap;
      static unsigned int mBufferCount;
      static std::map<int, std::tuple<std::string,int,void*> > mFdToFileNameMap;
      // HAL2 RELATED member variables end
      std::list<std::tuple<uint64_t ,void*, std::map<uint64_t , uint64_t> > > mReqList;
      uint64_t mReqCounter;
      FeatureRomHeader mFeatureRom;
      std::map<std::string, uint64_t> mCURangeMap;
      xrt::xclbin m_xclbin;

      std::set<unsigned int > mImportedBOs;
      exec_core* mCore;
      SWScheduler* mSWSch;
      bool mIsKdsSwEmu;
      bool mDeviceProcessInQemu;
      std::string mFpgaDevice;
  };

  class GraphType {
    // Core device to which the graph belongs.  The core device
    // has been loaded with an xclbin from which meta data can
    // be extracted
  public:
    GraphType(xclswemuhal2::SwEmuShim* handle, const char* graph) {
      _deviceHandle = handle;
      //_xclbin_uuid = xclbin_uuid;
      _graph = graph;
      graphHandle = mGraphHandle++;
      _state = graph_state::stop;
      _name = "";
      _startTime= 0;
    }
    xclswemuhal2::SwEmuShim*  getDeviceHandle() {  return _deviceHandle;  }
    const char*  getGraphName() { return _graph; }
    unsigned int  getGraphHandle() { return graphHandle; }
  private:
    xclswemuhal2::SwEmuShim*  _deviceHandle;
    //const uuid_t _xclbin_uuid;
    const char* _graph;
    unsigned int graphHandle;
    enum class graph_state : unsigned short
    {
      stop = 0,
      reset = 1,
      running = 2,
      suspend = 3,
      end = 4,
    };
    graph_state _state;
    std::string _name;
    uint64_t _startTime;
    /* This is the collections of rtps that are used. */
    std::vector<std::string> rtps;
    static unsigned int mGraphHandle;
  };
  extern std::map<unsigned int, SwEmuShim*> devices;
}

#endif