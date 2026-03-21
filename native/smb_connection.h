#pragma once

#include <napi.h>
#include <uv.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern "C" {
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/smb2-errors.h>
}

class SmbConnectionWrap : public Napi::ObjectWrap<SmbConnectionWrap> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  static Napi::Value Connect(const Napi::CallbackInfo& info);
  static Napi::FunctionReference constructor;

  SmbConnectionWrap(const Napi::CallbackInfo& info);
  ~SmbConnectionWrap() override;

private:
  struct PendingOperation {
    explicit PendingOperation(SmbConnectionWrap* owner);
    virtual ~PendingOperation() = default;

    SmbConnectionWrap* owner;
    Napi::Promise::Deferred deferred;
    bool settled;
    std::string path;
  };

  struct ConnectOperation final : PendingOperation {
    explicit ConnectOperation(SmbConnectionWrap* owner);
    Napi::ObjectReference self;
    bool signing_required;
    std::string server;
    std::string share;
    std::string username;
  };

  struct StatOperation final : PendingOperation {
    explicit StatOperation(SmbConnectionWrap* owner);
    smb2_stat_64 stat;
  };

  struct OpenOperation final : PendingOperation {
    explicit OpenOperation(SmbConnectionWrap* owner);
  };

  struct ReadOperation final : PendingOperation {
    explicit ReadOperation(SmbConnectionWrap* owner);
    std::vector<uint8_t> buffer;
  };

  struct WriteOperation final : PendingOperation {
    explicit WriteOperation(SmbConnectionWrap* owner);
    std::vector<uint8_t> buffer;
  };

  struct HandleOperation final : PendingOperation {
    explicit HandleOperation(SmbConnectionWrap* owner, uint32_t handle_id);
    uint32_t handle_id;
  };

  struct RenameOperation final : PendingOperation {
    explicit RenameOperation(SmbConnectionWrap* owner);
    std::string new_path;
  };

  struct FileHandleEntry {
    smb2fh* handle;
    std::string path;
  };

  struct DirHandleEntry {
    smb2dir* handle;
    std::string path;
  };

  struct PollState {
    uv_poll_t handle;
    SmbConnectionWrap* owner;
    t_socket fd;
  };

  bool EnsureContext();
  bool IsConnectionReady() const;
  Napi::Value RejectNotReady(Napi::Env env) const;
  void RegisterPending(PendingOperation* operation);
  void FinishPending(PendingOperation* operation);
  void RejectPending(PendingOperation* operation, const std::string& code, const std::string& message, int status, int nterror, const std::string* path = nullptr);
  void RejectAllPending(const std::string& code, const std::string& message);

  Napi::Object CreateError(const std::string& code, const std::string& message, int status, int nterror, const std::string* path = nullptr) const;
  std::string ClassifyError(int status, int nterror, const std::string& message, bool signing_required = false) const;
  std::string LastErrorMessage() const;
  int LastNtError() const;

  void StartPoll(t_socket fd);
  void StopPoll();
  void ApplyPollEvents();
  void StartServiceTimer();
  void StopServiceTimer();
  void CleanupContext();
  void HandleFatal(const std::string& message);

  static void OnFdChanged(struct smb2_context* smb2, t_socket fd, int cmd);
  static void OnEventsChanged(struct smb2_context* smb2, t_socket fd, int events);
  static void OnPollEvent(uv_poll_t* handle, int status, int events);
  static void OnPollClosed(uv_handle_t* handle);
  static void OnServiceTimer(uv_timer_t* handle);

  static void OnConnectComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data);
  static void OnDisconnectComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data);
  static void OnStatComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data);
  static void OnOpenComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data);
  static void OnReadComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data);
  static void OnWriteComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data);
  static void OnCloseComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data);
  static void OnFtruncateComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data);
  static void OnPathComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data);
  static void OnRenameComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data);
  static void OnOpendirComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data);

  Napi::Value Disconnect(const Napi::CallbackInfo& info);
  Napi::Value Stat(const Napi::CallbackInfo& info);
  Napi::Value Open(const Napi::CallbackInfo& info);
  Napi::Value Read(const Napi::CallbackInfo& info);
  Napi::Value Write(const Napi::CallbackInfo& info);
  Napi::Value Ftruncate(const Napi::CallbackInfo& info);
  Napi::Value Close(const Napi::CallbackInfo& info);
  Napi::Value Mkdir(const Napi::CallbackInfo& info);
  Napi::Value Rmdir(const Napi::CallbackInfo& info);
  Napi::Value Unlink(const Napi::CallbackInfo& info);
  Napi::Value Rename(const Napi::CallbackInfo& info);
  Napi::Value Opendir(const Napi::CallbackInfo& info);
  Napi::Value Readdir(const Napi::CallbackInfo& info);
  Napi::Value Closedir(const Napi::CallbackInfo& info);
  Napi::Value GetMaxReadSize(const Napi::CallbackInfo& info);
  Napi::Value GetMaxWriteSize(const Napi::CallbackInfo& info);
  Napi::Value GetConnected(const Napi::CallbackInfo& info);

  Napi::Object StatToJs(const smb2_stat_64& stat, const std::string& path, const std::string& name) const;
  std::string Basename(const std::string& path) const;

  smb2_context* context_;
  bool connected_;
  bool disconnecting_;
  int timeout_sec_;
  int poll_events_;
  PollState* poll_state_;
  uv_timer_t service_timer_;
  bool service_timer_initialized_;
  std::unordered_map<uint32_t, FileHandleEntry> file_handles_;
  std::unordered_map<uint32_t, DirHandleEntry> dir_handles_;
  uint32_t next_file_handle_;
  uint32_t next_dir_handle_;
  std::unordered_set<PendingOperation*> pending_;

  static std::unordered_map<smb2_context*, SmbConnectionWrap*> contexts_;
};
