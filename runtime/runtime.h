/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_RUNTIME_RUNTIME_H_
#define ART_RUNTIME_RUNTIME_H_

#include <jni.h>
#include <stdio.h>

#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

#include "base/macros.h"
#include "base/stringpiece.h"
#include "gc/heap.h"
#include "globals.h"
#include "instruction_set.h"
#include "instrumentation.h"
#include "jobject_comparator.h"
#include "locks.h"
#include "root_visitor.h"
#include "runtime_stats.h"
#include "safe_map.h"

namespace art {

namespace gc {
  class Heap;
}
namespace mirror {
  class AbstractMethod;
  class ClassLoader;
  template<class T> class PrimitiveArray;
  typedef PrimitiveArray<int8_t> ByteArray;
  class String;
  class Throwable;
}  // namespace mirror
class ClassLinker;
class DexFile;
class InternTable;
struct JavaVMExt;
class MonitorList;
class SignalCatcher;
class ThreadList;
class Trace;

class Runtime {
 public:
  typedef std::vector<std::pair<std::string, const void*> > Options;

  // In small mode, apps with fewer than this number of methods will be compiled
  // anyways.
  // TODO: come up with a reasonable default.
  static const size_t kDefaultSmallModeMethodThreshold = 0;

  // In small mode, methods smaller than this dex op count limit will get compiled
  // anyways.
  // TODO: come up with a reasonable default.
  static const size_t kDefaultSmallModeMethodDexSizeLimit = 300;

  class ParsedOptions {
   public:
    // returns null if problem parsing and ignore_unrecognized is false
    static ParsedOptions* Create(const Options& options, bool ignore_unrecognized);

    const std::vector<const DexFile*>* boot_class_path_;
    std::string boot_class_path_string_;
    std::string class_path_string_;
    std::string host_prefix_;
    std::string image_;
    bool check_jni_;
    std::string jni_trace_;
    bool is_compiler_;
    bool is_zygote_;
    bool interpreter_only_;
    bool is_concurrent_gc_enabled_;
    size_t heap_initial_size_;
    size_t heap_maximum_size_;
    size_t heap_growth_limit_;
    size_t heap_gc_threads_;
    size_t heap_min_free_;
    size_t heap_max_free_;
    double heap_target_utilization_;
    size_t stack_size_;
    bool low_memory_mode_;
    size_t lock_profiling_threshold_;
    std::string stack_trace_file_;
    bool method_trace_;
    std::string method_trace_file_;
    size_t method_trace_file_size_;
    bool (*hook_is_sensitive_thread_)();
    jint (*hook_vfprintf_)(FILE* stream, const char* format, va_list ap);
    void (*hook_exit_)(jint status);
    void (*hook_abort_)();
    std::vector<std::string> properties_;
    bool small_mode_;

    size_t small_mode_method_threshold_;
    size_t small_mode_method_dex_size_limit_;

    bool sea_ir_mode_;

   private:
    ParsedOptions() {}
  };

  // Creates and initializes a new runtime.
  static bool Create(const Options& options, bool ignore_unrecognized)
      SHARED_TRYLOCK_FUNCTION(true, Locks::mutator_lock_);

  bool IsCompiler() const {
    return is_compiler_;
  }

  bool IsZygote() const {
    return is_zygote_;
  }

  bool IsConcurrentGcEnabled() const {
    return is_concurrent_gc_enabled_;
  }

#ifdef ART_SEA_IR_MODE
  bool IsSeaIRMode() const {
    return sea_ir_mode_;
  }
#endif

  void SetSeaIRMode(bool sea_ir_mode) {
    sea_ir_mode_ = sea_ir_mode;
  }

  bool IsSmallMode() const {
      return small_mode_;
  }


  void SetSmallMode(bool small_mode) {
      small_mode_ = small_mode;
  }

  size_t GetSmallModeMethodThreshold() const {
      return small_mode_method_threshold_;
  }

  size_t GetSmallModeMethodDexSizeLimit() const {
      return small_mode_method_dex_size_limit_;
  }

  const std::string& GetHostPrefix() const {
    DCHECK(!IsStarted());
    return host_prefix_;
  }

  // Starts a runtime, which may cause threads to be started and code to run.
  bool Start() UNLOCK_FUNCTION(Locks::mutator_lock_);

  bool IsShuttingDown() const EXCLUSIVE_LOCKS_REQUIRED(Locks::runtime_shutdown_lock_) {
    return shutting_down_;
  }

  size_t NumberOfThreadsBeingBorn() const EXCLUSIVE_LOCKS_REQUIRED(Locks::runtime_shutdown_lock_) {
    return threads_being_born_;
  }

  void StartThreadBirth() EXCLUSIVE_LOCKS_REQUIRED(Locks::runtime_shutdown_lock_) {
    threads_being_born_++;
  }

  void EndThreadBirth() EXCLUSIVE_LOCKS_REQUIRED(Locks::runtime_shutdown_lock_);

  bool IsStarted() const {
    return started_;
  }

  bool IsFinishedStarting() const {
    return finished_starting_;
  }

  static Runtime* Current() {
    return instance_;
  }

  // Aborts semi-cleanly. Used in the implementation of LOG(FATAL), which most
  // callers should prefer.
  // This isn't marked ((noreturn)) because then gcc will merge multiple calls
  // in a single function together. This reduces code size slightly, but means
  // that the native stack trace we get may point at the wrong call site.
  static void Abort() LOCKS_EXCLUDED(Locks::abort_lock_);

  // Returns the "main" ThreadGroup, used when attaching user threads.
  jobject GetMainThreadGroup() const;

  // Returns the "system" ThreadGroup, used when attaching our internal threads.
  jobject GetSystemThreadGroup() const;

  // Attaches the calling native thread to the runtime.
  bool AttachCurrentThread(const char* thread_name, bool as_daemon, jobject thread_group,
                           bool create_peer);

  void CallExitHook(jint status);

  // Detaches the current native thread from the runtime.
  void DetachCurrentThread() LOCKS_EXCLUDED(Locks::mutator_lock_);

  void DumpForSigQuit(std::ostream& os)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void DumpLockHolders(std::ostream& os);

  ~Runtime();

  const std::string& GetBootClassPathString() const {
    return boot_class_path_string_;
  }

  const std::string& GetClassPathString() const {
    return class_path_string_;
  }

  ClassLinker* GetClassLinker() const {
    return class_linker_;
  }

  size_t GetDefaultStackSize() const {
    return default_stack_size_;
  }

  gc::Heap* GetHeap() const {
    return heap_;
  }

  InternTable* GetInternTable() const {
    return intern_table_;
  }

  JavaVMExt* GetJavaVM() const {
    return java_vm_;
  }

  MonitorList* GetMonitorList() const {
    return monitor_list_;
  }

  mirror::Throwable* GetPreAllocatedOutOfMemoryError() {
    return pre_allocated_OutOfMemoryError_;
  }

  const std::vector<std::string>& GetProperties() const {
    return properties_;
  }

  ThreadList* GetThreadList() const {
    return thread_list_;
  }

  const char* GetVersion() const {
    return "2.0.0";
  }

  // Visit all the roots. If only_dirty is true then non-dirty roots won't be visited. If
  // clean_dirty is true then dirty roots will be marked as non-dirty after visiting.
  void VisitRoots(RootVisitor* visitor, void* arg, bool only_dirty, bool clean_dirty)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Visit all of the roots we can do safely do concurrently.
  void VisitConcurrentRoots(RootVisitor* visitor, void* arg, bool only_dirty, bool clean_dirty);

  // Visit all of the non thread roots, we can do this with mutators unpaused.
  void VisitNonThreadRoots(RootVisitor* visitor, void* arg);

  // Visit all other roots which must be done with mutators suspended.
  void VisitNonConcurrentRoots(RootVisitor* visitor, void* arg)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Returns a special method that calls into a trampoline for runtime method resolution
  mirror::AbstractMethod* GetResolutionMethod() const {
    CHECK(HasResolutionMethod());
    return resolution_method_;
  }

  bool HasResolutionMethod() const {
    return resolution_method_ != NULL;
  }

  void SetResolutionMethod(mirror::AbstractMethod* method) {
    resolution_method_ = method;
  }

  mirror::AbstractMethod* CreateResolutionMethod() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Returns a special method that describes all callee saves being spilled to the stack.
  enum CalleeSaveType {
    kSaveAll,
    kRefsOnly,
    kRefsAndArgs,
    kLastCalleeSaveType  // Value used for iteration
  };

  bool HasCalleeSaveMethod(CalleeSaveType type) const {
    return callee_save_methods_[type] != NULL;
  }

  mirror::AbstractMethod* GetCalleeSaveMethod(CalleeSaveType type) const {
    DCHECK(HasCalleeSaveMethod(type));
    return callee_save_methods_[type];
  }

  void SetCalleeSaveMethod(mirror::AbstractMethod* method, CalleeSaveType type);

  mirror::AbstractMethod* CreateCalleeSaveMethod(InstructionSet instruction_set,
                                                 CalleeSaveType type)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  mirror::AbstractMethod* CreateRefOnlyCalleeSaveMethod(InstructionSet instruction_set)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  mirror::AbstractMethod* CreateRefAndArgsCalleeSaveMethod(InstructionSet instruction_set)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  int32_t GetStat(int kind);

  RuntimeStats* GetStats() {
    return &stats_;
  }

  bool HasStatsEnabled() const {
    return stats_enabled_;
  }

  void ResetStats(int kinds);

  void SetStatsEnabled(bool new_state);

  bool PreZygoteFork();
  bool InitZygote();
  void DidForkFromZygote();

  instrumentation::Instrumentation* GetInstrumentation() {
    return &instrumentation_;
  }

  bool UseCompileTimeClassPath() const {
    return use_compile_time_class_path_;
  }

  const std::vector<const DexFile*>& GetCompileTimeClassPath(jobject class_loader);
  void SetCompileTimeClassPath(jobject class_loader, std::vector<const DexFile*>& class_path);

 private:
  static void InitPlatformSignalHandlers();

  Runtime();

  void BlockSignals();

  bool Init(const Options& options, bool ignore_unrecognized)
      SHARED_TRYLOCK_FUNCTION(true, Locks::mutator_lock_);
  void InitNativeMethods() LOCKS_EXCLUDED(Locks::mutator_lock_);
  void InitThreadGroups(Thread* self);
  void RegisterRuntimeNativeMethods(JNIEnv* env);

  void StartDaemonThreads();
  void StartSignalCatcher();

  // A pointer to the active runtime or NULL.
  static Runtime* instance_;

  bool is_compiler_;
  bool is_zygote_;
  bool is_concurrent_gc_enabled_;

  bool small_mode_;
  size_t small_mode_method_threshold_;
  size_t small_mode_method_dex_size_limit_;

  bool sea_ir_mode_;

  // The host prefix is used during cross compilation. It is removed
  // from the start of host paths such as:
  //    $ANDROID_PRODUCT_OUT/system/framework/boot.oat
  // to produce target paths such as
  //    /system/framework/boot.oat
  // Similarly it is prepended to target paths to arrive back at a
  // host past. In both cases this is necessary because image and oat
  // files embedded expect paths of dependent files (an image points
  // to an oat file and an oat files to one or more dex files). These
  // files contain the expected target path.
  std::string host_prefix_;

  std::string boot_class_path_string_;
  std::string class_path_string_;
  std::vector<std::string> properties_;

  // The default stack size for managed threads created by the runtime.
  size_t default_stack_size_;

  gc::Heap* heap_;

  MonitorList* monitor_list_;

  ThreadList* thread_list_;

  InternTable* intern_table_;

  ClassLinker* class_linker_;

  SignalCatcher* signal_catcher_;
  std::string stack_trace_file_;

  JavaVMExt* java_vm_;

  mirror::Throwable* pre_allocated_OutOfMemoryError_;

  mirror::AbstractMethod* callee_save_methods_[kLastCalleeSaveType];

  mirror::AbstractMethod* resolution_method_;

  // As returned by ClassLoader.getSystemClassLoader()
  mirror::ClassLoader* system_class_loader_;

  // A non-zero value indicates that a thread has been created but not yet initialized. Guarded by
  // the shutdown lock so that threads aren't born while we're shutting down.
  size_t threads_being_born_ GUARDED_BY(Locks::runtime_shutdown_lock_);

  // Waited upon until no threads are being born.
  UniquePtr<ConditionVariable> shutdown_cond_ GUARDED_BY(Locks::runtime_shutdown_lock_);

  // Set when runtime shutdown is past the point that new threads may attach.
  bool shutting_down_ GUARDED_BY(Locks::runtime_shutdown_lock_);

  // The runtime is starting to shutdown but is blocked waiting on shutdown_cond_.
  bool shutting_down_started_ GUARDED_BY(Locks::runtime_shutdown_lock_);

  bool started_;

  // New flag added which tells us if the runtime has finished starting. If
  // this flag is set then the Daemon threads are created and the class loader
  // is created. This flag is needed for knowing if its safe to request CMS.
  bool finished_starting_;

  // Hooks supported by JNI_CreateJavaVM
  jint (*vfprintf_)(FILE* stream, const char* format, va_list ap);
  void (*exit_)(jint status);
  void (*abort_)();

  bool stats_enabled_;
  RuntimeStats stats_;

  bool method_trace_;
  std::string method_trace_file_;
  size_t method_trace_file_size_;
  instrumentation::Instrumentation instrumentation_;

  typedef SafeMap<jobject, std::vector<const DexFile*>, JobjectComparator> CompileTimeClassPaths;
  CompileTimeClassPaths compile_time_class_paths_;
  bool use_compile_time_class_path_;

  jobject main_thread_group_;
  jobject system_thread_group_;

  DISALLOW_COPY_AND_ASSIGN(Runtime);
};

}  // namespace art

#endif  // ART_RUNTIME_RUNTIME_H_
