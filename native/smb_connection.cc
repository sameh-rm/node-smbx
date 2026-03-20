#include "smb_connection.h"

#include <fcntl.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr uint64_t kServiceTickMs = 100;
}

Napi::FunctionReference SmbConnectionWrap::constructor;
std::unordered_map<smb2_context*, SmbConnectionWrap*> SmbConnectionWrap::contexts_;

SmbConnectionWrap::PendingOperation::PendingOperation(SmbConnectionWrap* owner_in)
    : owner(owner_in), deferred(Napi::Promise::Deferred::New(owner_in->Env())), settled(false) {}

SmbConnectionWrap::ConnectOperation::ConnectOperation(SmbConnectionWrap* owner_in)
    : PendingOperation(owner_in), signing_required(false) {}

SmbConnectionWrap::StatOperation::StatOperation(SmbConnectionWrap* owner_in)
    : PendingOperation(owner_in), stat{} {}

SmbConnectionWrap::OpenOperation::OpenOperation(SmbConnectionWrap* owner_in)
    : PendingOperation(owner_in) {}

SmbConnectionWrap::ReadOperation::ReadOperation(SmbConnectionWrap* owner_in)
    : PendingOperation(owner_in) {}

SmbConnectionWrap::WriteOperation::WriteOperation(SmbConnectionWrap* owner_in)
    : PendingOperation(owner_in) {}

SmbConnectionWrap::HandleOperation::HandleOperation(SmbConnectionWrap* owner_in, uint32_t handle)
    : PendingOperation(owner_in), handle_id(handle) {}

SmbConnectionWrap::RenameOperation::RenameOperation(SmbConnectionWrap* owner_in)
    : PendingOperation(owner_in) {}

Napi::Object SmbConnectionWrap::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function klass = DefineClass(
      env,
      "SmbConnection",
      {
          StaticMethod("connect", &SmbConnectionWrap::Connect),
          InstanceMethod("disconnect", &SmbConnectionWrap::Disconnect),
          InstanceMethod("stat", &SmbConnectionWrap::Stat),
          InstanceMethod("open", &SmbConnectionWrap::Open),
          InstanceMethod("read", &SmbConnectionWrap::Read),
          InstanceMethod("write", &SmbConnectionWrap::Write),
          InstanceMethod("ftruncate", &SmbConnectionWrap::Ftruncate),
          InstanceMethod("close", &SmbConnectionWrap::Close),
          InstanceMethod("mkdir", &SmbConnectionWrap::Mkdir),
          InstanceMethod("rmdir", &SmbConnectionWrap::Rmdir),
          InstanceMethod("unlink", &SmbConnectionWrap::Unlink),
          InstanceMethod("rename", &SmbConnectionWrap::Rename),
          InstanceMethod("opendir", &SmbConnectionWrap::Opendir),
          InstanceMethod("readdir", &SmbConnectionWrap::Readdir),
          InstanceMethod("closedir", &SmbConnectionWrap::Closedir),
          InstanceMethod("getMaxReadSize", &SmbConnectionWrap::GetMaxReadSize),
          InstanceMethod("getMaxWriteSize", &SmbConnectionWrap::GetMaxWriteSize),
          InstanceAccessor("connected", &SmbConnectionWrap::GetConnected, nullptr),
      });

  constructor = Napi::Persistent(klass);
  constructor.SuppressDestruct();
  exports.Set("SmbConnection", klass);
  return exports;
}

SmbConnectionWrap::SmbConnectionWrap(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<SmbConnectionWrap>(info),
      context_(nullptr),
      connected_(false),
      disconnecting_(false),
      timeout_sec_(0),
      poll_events_(0),
      poll_state_(nullptr),
      service_timer_{},
      service_timer_initialized_(false),
      file_handles_(),
      dir_handles_(),
      next_file_handle_(1),
      next_dir_handle_(1),
      pending_() {}

SmbConnectionWrap::~SmbConnectionWrap() {
  CleanupContext();
}

bool SmbConnectionWrap::EnsureContext() {
  if (context_ != nullptr) {
    return true;
  }

  context_ = smb2_init_context();
  if (context_ == nullptr) {
    return false;
  }

  contexts_[context_] = this;
  smb2_fd_event_callbacks(context_, &SmbConnectionWrap::OnFdChanged, &SmbConnectionWrap::OnEventsChanged);
  smb2_set_authentication(context_, SMB2_SEC_NTLMSSP);
  if (timeout_sec_ > 0) {
    smb2_set_timeout(context_, timeout_sec_);
  }
  return true;
}

void SmbConnectionWrap::RegisterPending(PendingOperation* operation) {
  pending_.insert(operation);
  StartServiceTimer();
  Ref();
}

void SmbConnectionWrap::FinishPending(PendingOperation* operation) {
  auto it = pending_.find(operation);
  if (it != pending_.end()) {
    pending_.erase(it);
    Unref();
  }
  if (pending_.empty()) {
    StopServiceTimer();
  }
  delete operation;
}

Napi::Object SmbConnectionWrap::CreateError(const std::string& code,
                                            const std::string& message,
                                            int status,
                                            int nterror,
                                            const std::string* path) const {
  Napi::Error error = Napi::Error::New(Env(), message);
  Napi::Object value = error.Value();
  value.Set("code", Napi::String::New(Env(), code));
  value.Set("errno", Napi::Number::New(Env(), status));
  value.Set("nterror", Napi::Number::New(Env(), nterror));
  if (path != nullptr) {
    value.Set("path", Napi::String::New(Env(), *path));
  }
  return value;
}

std::string SmbConnectionWrap::ClassifyError(int status,
                                             int nterror,
                                             const std::string& message,
                                             bool signing_required) const {
  if (signing_required && message.find("sign") != std::string::npos) {
    return "SIGNING_REQUIRED";
  }

  switch (nterror) {
    case SMB2_STATUS_LOGON_FAILURE:
      return "AUTH";
    case SMB2_STATUS_IO_TIMEOUT:
      return "TIMEOUT";
    case SMB2_STATUS_OBJECT_NAME_NOT_FOUND:
      return "NOT_FOUND";
    case SMB2_STATUS_OBJECT_NAME_COLLISION:
      return "ALREADY_EXISTS";
    default:
      break;
  }

  switch (status) {
    case -ENOENT:
      return "NOT_FOUND";
    case -EEXIST:
      return "ALREADY_EXISTS";
    case -ETIMEDOUT:
      return "TIMEOUT";
    case -EINVAL:
    case -EBADF:
      return "INVALID_STATE";
    default:
      break;
  }

  if (nterror != 0) {
    return "PROTOCOL";
  }
  return "CONNECTION";
}

std::string SmbConnectionWrap::LastErrorMessage() const {
  if (context_ == nullptr) {
    return "SMB context is not available";
  }
  const char* message = smb2_get_error(context_);
  if (message == nullptr || message[0] == '\0') {
    return "SMB operation failed";
  }
  return std::string(message);
}

int SmbConnectionWrap::LastNtError() const {
  if (context_ == nullptr) {
    return 0;
  }
  return smb2_get_nterror(context_);
}

void SmbConnectionWrap::RejectPending(PendingOperation* operation,
                                      const std::string& code,
                                      const std::string& message,
                                      int status,
                                      int nterror,
                                      const std::string* path) {
  if (operation->settled) {
    return;
  }
  operation->deferred.Reject(CreateError(code, message, status, nterror, path));
  operation->settled = true;
}

void SmbConnectionWrap::RejectAllPending(const std::string& code, const std::string& message) {
  std::vector<PendingOperation*> operations;
  operations.reserve(pending_.size());
  for (PendingOperation* operation : pending_) {
    operations.push_back(operation);
  }
  for (PendingOperation* operation : operations) {
    RejectPending(operation, code, message, -ECONNRESET, 0, operation->path.empty() ? nullptr : &operation->path);
  }
}

void SmbConnectionWrap::StartPoll(t_socket fd) {
  if (poll_state_ != nullptr && poll_state_->fd == fd) {
    return;
  }

  StopPoll();

  uv_loop_t* loop = nullptr;
  napi_get_uv_event_loop(Env(), &loop);
  auto* state = new PollState{};
  state->owner = this;
  state->fd = fd;
  state->handle.data = state;

  const int rc = uv_poll_init_socket(loop, &state->handle, fd);
  if (rc != 0) {
    delete state;
    HandleFatal("Failed to initialize uv_poll for SMB socket");
    return;
  }

  poll_state_ = state;
  ApplyPollEvents();
}

void SmbConnectionWrap::StopPoll() {
  if (poll_state_ == nullptr) {
    return;
  }

  uv_poll_stop(&poll_state_->handle);
  uv_close(reinterpret_cast<uv_handle_t*>(&poll_state_->handle), &SmbConnectionWrap::OnPollClosed);
  poll_state_ = nullptr;
}

void SmbConnectionWrap::ApplyPollEvents() {
  if (poll_state_ == nullptr) {
    return;
  }

  int uv_events = 0;
  if ((poll_events_ & POLLIN) != 0) {
    uv_events |= UV_READABLE;
  }
  if ((poll_events_ & POLLOUT) != 0) {
    uv_events |= UV_WRITABLE;
  }

  if (uv_events == 0) {
    uv_poll_stop(&poll_state_->handle);
    return;
  }

  uv_poll_start(&poll_state_->handle, uv_events, &SmbConnectionWrap::OnPollEvent);
}

void SmbConnectionWrap::StartServiceTimer() {
  uv_loop_t* loop = nullptr;
  napi_get_uv_event_loop(Env(), &loop);
  if (!service_timer_initialized_) {
    uv_timer_init(loop, &service_timer_);
    service_timer_.data = this;
    service_timer_initialized_ = true;
  }

  uv_timer_start(&service_timer_, &SmbConnectionWrap::OnServiceTimer, kServiceTickMs, kServiceTickMs);
}

void SmbConnectionWrap::StopServiceTimer() {
  if (!service_timer_initialized_) {
    return;
  }
  uv_timer_stop(&service_timer_);
}

void SmbConnectionWrap::CleanupContext() {
  StopPoll();
  StopServiceTimer();

  file_handles_.clear();
  dir_handles_.clear();

  if (context_ != nullptr) {
    contexts_.erase(context_);
    smb2_destroy_context(context_);
    context_ = nullptr;
  }

  connected_ = false;
  disconnecting_ = false;
  poll_events_ = 0;
}

void SmbConnectionWrap::HandleFatal(const std::string& message) {
  RejectAllPending("CONNECTION", message);
  CleanupContext();
}

void SmbConnectionWrap::OnFdChanged(struct smb2_context* smb2, t_socket fd, int cmd) {
  auto it = contexts_.find(smb2);
  if (it == contexts_.end()) {
    return;
  }
  SmbConnectionWrap* owner = it->second;
  if (cmd == SMB2_ADD_FD) {
    owner->StartPoll(fd);
  } else {
    owner->StopPoll();
  }
}

void SmbConnectionWrap::OnEventsChanged(struct smb2_context* smb2, t_socket fd, int events) {
  (void)fd;
  auto it = contexts_.find(smb2);
  if (it == contexts_.end()) {
    return;
  }
  SmbConnectionWrap* owner = it->second;
  owner->poll_events_ = events;
  owner->ApplyPollEvents();
}

void SmbConnectionWrap::OnPollEvent(uv_poll_t* handle, int status, int events) {
  auto* state = static_cast<PollState*>(handle->data);
  SmbConnectionWrap* owner = state->owner;
  if (owner->context_ == nullptr) {
    return;
  }
  Napi::HandleScope scope(owner->Env());
  if (status < 0) {
    owner->HandleFatal("SMB socket polling failed");
    return;
  }

  int revents = 0;
  if ((events & UV_READABLE) != 0) {
    revents |= POLLIN;
  }
  if ((events & UV_WRITABLE) != 0) {
    revents |= POLLOUT;
  }

  if (revents == 0) {
    return;
  }

  if (smb2_service(owner->context_, revents) < 0) {
    owner->HandleFatal(owner->LastErrorMessage());
    return;
  }
  owner->ApplyPollEvents();
}

void SmbConnectionWrap::OnPollClosed(uv_handle_t* handle) {
  auto* state = static_cast<PollState*>(handle->data);
  delete state;
}

void SmbConnectionWrap::OnServiceTimer(uv_timer_t* handle) {
  auto* owner = static_cast<SmbConnectionWrap*>(handle->data);
  if (owner == nullptr || owner->context_ == nullptr) {
    return;
  }
  Napi::HandleScope scope(owner->Env());
  if (smb2_service(owner->context_, 0) < 0) {
    owner->HandleFatal(owner->LastErrorMessage());
  }
}

std::string SmbConnectionWrap::Basename(const std::string& path) const {
  if (path.empty()) {
    return "";
  }
  const size_t pos = path.find_last_of('\\');
  if (pos == std::string::npos) {
    return path;
  }
  return path.substr(pos + 1);
}

Napi::Object SmbConnectionWrap::StatToJs(const smb2_stat_64& stat,
                                         const std::string& path,
                                         const std::string& name) const {
  Napi::Object result = Napi::Object::New(Env());
  result.Set("path", Napi::String::New(Env(), path));
  result.Set("name", Napi::String::New(Env(), name));
  result.Set("size", Napi::BigInt::New(Env(), stat.smb2_size));
  result.Set("isDirectory", Napi::Boolean::New(Env(), stat.smb2_type == SMB2_TYPE_DIRECTORY));
  result.Set("isSymlink", Napi::Boolean::New(Env(), stat.smb2_type == SMB2_TYPE_LINK));

  auto setDate = [&](const char* key, uint64_t seconds, uint64_t nanos) {
    if (seconds == 0 && nanos == 0) {
      return;
    }
    const double milliseconds = static_cast<double>(seconds) * 1000.0 + static_cast<double>(nanos) / 1000000.0;
    result.Set(key, Napi::Date::New(Env(), milliseconds));
  };

  setDate("accessedAt", stat.smb2_atime, stat.smb2_atime_nsec);
  setDate("modifiedAt", stat.smb2_mtime, stat.smb2_mtime_nsec);
  setDate("createdAt", stat.smb2_btime, stat.smb2_btime_nsec);

  return result;
}

Napi::Value SmbConnectionWrap::Connect(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::TypeError::New(env, "connect(options) requires an options object").ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Object options = info[0].As<Napi::Object>();
  auto requireString = [&](const char* key) -> std::string {
    Napi::Value value = options.Get(key);
    if (!value.IsString()) {
      Napi::TypeError::New(env, std::string(key) + " must be a string").ThrowAsJavaScriptException();
      return std::string();
    }
    return value.As<Napi::String>().Utf8Value();
  };

  const std::string server = requireString("server");
  if (env.IsExceptionPending()) {
    return env.Null();
  }
  const std::string share = requireString("share");
  if (env.IsExceptionPending()) {
    return env.Null();
  }
  const std::string username = requireString("username");
  if (env.IsExceptionPending()) {
    return env.Null();
  }
  const std::string password = requireString("password");
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  std::string domain;
  Napi::Value domainValue = options.Get("domain");
  if (!domainValue.IsUndefined() && !domainValue.IsNull()) {
    if (!domainValue.IsString()) {
      Napi::TypeError::New(env, "domain must be a string").ThrowAsJavaScriptException();
      return env.Null();
    }
    domain = domainValue.As<Napi::String>().Utf8Value();
  }

  std::string signing = "auto";
  Napi::Value signingValue = options.Get("signing");
  if (!signingValue.IsUndefined() && !signingValue.IsNull()) {
    signing = signingValue.As<Napi::String>().Utf8Value();
  }

  uint32_t timeout = 0;
  Napi::Value timeoutValue = options.Get("timeoutSec");
  if (!timeoutValue.IsUndefined() && !timeoutValue.IsNull()) {
    timeout = timeoutValue.As<Napi::Number>().Uint32Value();
  }

  std::string serverAddress = server;
  Napi::Value portValue = options.Get("port");
  if (!portValue.IsUndefined() && !portValue.IsNull()) {
    const uint32_t port = portValue.As<Napi::Number>().Uint32Value();
    serverAddress += ":" + std::to_string(port);
  }

  Napi::Object object = constructor.New({});
  auto* owner = Napi::ObjectWrap<SmbConnectionWrap>::Unwrap(object);
  owner->timeout_sec_ = static_cast<int>(timeout);

  auto* operation = new ConnectOperation(owner);
  operation->self = Napi::Persistent(object);
  operation->signing_required = signing == "required";
  operation->server = serverAddress;
  operation->share = share;
  operation->username = username;

  if (!owner->EnsureContext()) {
    operation->deferred.Reject(Napi::Error::New(env, "Failed to initialize libsmb2 context").Value());
    Napi::Promise promise = operation->deferred.Promise();
    delete operation;
    return promise;
  }

  smb2_set_user(owner->context_, username.c_str());
  smb2_set_password(owner->context_, password.c_str());
  if (!domain.empty()) {
    smb2_set_domain(owner->context_, domain.c_str());
  }

  const uint16_t securityMode = operation->signing_required
      ? static_cast<uint16_t>(SMB2_NEGOTIATE_SIGNING_ENABLED | SMB2_NEGOTIATE_SIGNING_REQUIRED)
      : static_cast<uint16_t>(SMB2_NEGOTIATE_SIGNING_ENABLED);
  smb2_set_security_mode(owner->context_, securityMode);

  owner->RegisterPending(operation);
  const int rc = smb2_connect_share_async(owner->context_, serverAddress.c_str(), share.c_str(), username.c_str(), &SmbConnectionWrap::OnConnectComplete, operation);
  if (rc < 0) {
    const std::string message = owner->LastErrorMessage();
    const int nterror = owner->LastNtError();
    owner->RejectPending(operation, owner->ClassifyError(rc, nterror, message, operation->signing_required), message, rc, nterror);
    owner->FinishPending(operation);
    owner->CleanupContext();
  }

  return operation->deferred.Promise();
}

void SmbConnectionWrap::OnConnectComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data) {
  (void)smb2;
  (void)command_data;
  auto* operation = static_cast<ConnectOperation*>(cb_data);
  SmbConnectionWrap* owner = operation->owner;
  Napi::HandleScope scope(owner->Env());

  if (operation->settled) {
    owner->FinishPending(operation);
    return;
  }

  if (status < 0) {
    const std::string message = owner->LastErrorMessage();
    const int nterror = owner->LastNtError();
    owner->RejectPending(operation, owner->ClassifyError(status, nterror, message, operation->signing_required), message, status, nterror);
    owner->CleanupContext();
    owner->FinishPending(operation);
    return;
  }

  owner->connected_ = true;
  owner->disconnecting_ = false;
  operation->deferred.Resolve(operation->self.Value());
  operation->settled = true;
  owner->FinishPending(operation);
}

Napi::Value SmbConnectionWrap::Disconnect(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  auto* operation = new PendingOperation(this);

  if (context_ == nullptr || !connected_) {
    operation->deferred.Resolve(env.Undefined());
    Napi::Promise promise = operation->deferred.Promise();
    delete operation;
    return promise;
  }

  disconnecting_ = true;
  RejectAllPending("CONNECTION", "Connection closed");
  RegisterPending(operation);

  const int rc = smb2_disconnect_share_async(context_, &SmbConnectionWrap::OnDisconnectComplete, operation);
  if (rc < 0) {
    const std::string message = LastErrorMessage();
    const int nterror = LastNtError();
    RejectPending(operation, ClassifyError(rc, nterror, message), message, rc, nterror);
    CleanupContext();
    FinishPending(operation);
  }

  return operation->deferred.Promise();
}

void SmbConnectionWrap::OnDisconnectComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data) {
  (void)smb2;
  (void)status;
  (void)command_data;
  auto* operation = static_cast<PendingOperation*>(cb_data);
  SmbConnectionWrap* owner = operation->owner;
  Napi::HandleScope scope(owner->Env());
  owner->StopPoll();
  owner->StopServiceTimer();
  owner->file_handles_.clear();
  owner->dir_handles_.clear();
  owner->connected_ = false;
  owner->disconnecting_ = false;
  owner->poll_events_ = 0;
  if (!operation->settled) {
    operation->deferred.Resolve(owner->Env().Undefined());
    operation->settled = true;
  }
  owner->FinishPending(operation);
}

Napi::Value SmbConnectionWrap::Stat(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (context_ == nullptr || !connected_ || disconnecting_) {
    auto deferred = Napi::Promise::Deferred::New(env);
    deferred.Reject(CreateError("INVALID_STATE", "Connection is not ready", -EINVAL, 0));
    return deferred.Promise();
  }
  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "stat(path) requires a string path").ThrowAsJavaScriptException();
    return env.Null();
  }

  auto* operation = new StatOperation(this);
  operation->path = info[0].As<Napi::String>().Utf8Value();
  RegisterPending(operation);
  const int rc = smb2_stat_async(context_, operation->path.c_str(), &operation->stat, &SmbConnectionWrap::OnStatComplete, operation);
  if (rc < 0) {
    const std::string message = LastErrorMessage();
    const int nterror = LastNtError();
    RejectPending(operation, ClassifyError(rc, nterror, message), message, rc, nterror, &operation->path);
    FinishPending(operation);
  }
  return operation->deferred.Promise();
}

void SmbConnectionWrap::OnStatComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data) {
  (void)smb2;
  (void)command_data;
  auto* operation = static_cast<StatOperation*>(cb_data);
  SmbConnectionWrap* owner = operation->owner;
  Napi::HandleScope scope(owner->Env());

  if (operation->settled) {
    owner->FinishPending(operation);
    return;
  }
  if (status < 0) {
    const std::string message = owner->LastErrorMessage();
    const int nterror = owner->LastNtError();
    owner->RejectPending(operation, owner->ClassifyError(status, nterror, message), message, status, nterror, &operation->path);
    owner->FinishPending(operation);
    return;
  }

  operation->deferred.Resolve(owner->StatToJs(operation->stat, operation->path, owner->Basename(operation->path)));
  operation->settled = true;
  owner->FinishPending(operation);
}

Napi::Value SmbConnectionWrap::Open(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (context_ == nullptr || !connected_ || disconnecting_) {
    auto deferred = Napi::Promise::Deferred::New(env);
    deferred.Reject(CreateError("INVALID_STATE", "Connection is not ready", -EINVAL, 0));
    return deferred.Promise();
  }
  if (info.Length() < 2 || !info[0].IsString() || !info[1].IsNumber()) {
    Napi::TypeError::New(env, "open(path, flags) requires a string path and numeric flags").ThrowAsJavaScriptException();
    return env.Null();
  }

  auto* operation = new OpenOperation(this);
  operation->path = info[0].As<Napi::String>().Utf8Value();
  const int flags = info[1].As<Napi::Number>().Int32Value();
  RegisterPending(operation);
  const int rc = smb2_open_async(context_, operation->path.c_str(), flags, &SmbConnectionWrap::OnOpenComplete, operation);
  if (rc < 0) {
    const std::string message = LastErrorMessage();
    const int nterror = LastNtError();
    RejectPending(operation, ClassifyError(rc, nterror, message), message, rc, nterror, &operation->path);
    FinishPending(operation);
  }
  return operation->deferred.Promise();
}

void SmbConnectionWrap::OnOpenComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data) {
  (void)smb2;
  auto* operation = static_cast<OpenOperation*>(cb_data);
  SmbConnectionWrap* owner = operation->owner;
  Napi::HandleScope scope(owner->Env());
  auto* handle = static_cast<smb2fh*>(command_data);

  if (operation->settled || owner->disconnecting_) {
    if (status == 0 && handle != nullptr && owner->context_ != nullptr) {
      smb2_close(owner->context_, handle);
    }
    if (!operation->settled) {
      owner->RejectPending(operation, "CONNECTION", "Connection closed", -ECONNRESET, 0, &operation->path);
    }
    owner->FinishPending(operation);
    return;
  }

  if (status < 0) {
    const std::string message = owner->LastErrorMessage();
    const int nterror = owner->LastNtError();
    owner->RejectPending(operation, owner->ClassifyError(status, nterror, message), message, status, nterror, &operation->path);
    owner->FinishPending(operation);
    return;
  }

  const uint32_t handleId = owner->next_file_handle_++;
  owner->file_handles_.emplace(handleId, FileHandleEntry{handle, operation->path});
  operation->deferred.Resolve(Napi::Number::New(owner->Env(), handleId));
  operation->settled = true;
  owner->FinishPending(operation);
}

Napi::Value SmbConnectionWrap::Read(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 3 || !info[0].IsNumber() || !info[1].IsBigInt() || !info[2].IsNumber()) {
    Napi::TypeError::New(env, "read(handle, offset, length) requires number, bigint, number").ThrowAsJavaScriptException();
    return env.Null();
  }

  const uint32_t handleId = info[0].As<Napi::Number>().Uint32Value();
  auto handleIt = file_handles_.find(handleId);
  if (handleIt == file_handles_.end()) {
    auto deferred = Napi::Promise::Deferred::New(env);
    deferred.Reject(CreateError("INVALID_STATE", "Unknown file handle", -EBADF, 0));
    return deferred.Promise();
  }

  bool lossless = false;
  const uint64_t offset = info[1].As<Napi::BigInt>().Uint64Value(&lossless);
  if (!lossless) {
    Napi::RangeError::New(env, "offset exceeds uint64 range").ThrowAsJavaScriptException();
    return env.Null();
  }
  const uint32_t length = info[2].As<Napi::Number>().Uint32Value();
  const uint32_t maxLength = smb2_get_max_read_size(context_);
  if (maxLength != 0 && length > maxLength) {
    auto deferred = Napi::Promise::Deferred::New(env);
    deferred.Reject(CreateError("INVALID_STATE", "Requested read length exceeds server maximum", -EINVAL, 0));
    return deferred.Promise();
  }

  auto* operation = new ReadOperation(this);
  operation->buffer.resize(length);
  RegisterPending(operation);
  const int rc = smb2_pread_async(context_, handleIt->second.handle, operation->buffer.data(), length, offset, &SmbConnectionWrap::OnReadComplete, operation);
  if (rc < 0) {
    const std::string message = LastErrorMessage();
    const int nterror = LastNtError();
    RejectPending(operation, ClassifyError(rc, nterror, message), message, rc, nterror, &handleIt->second.path);
    FinishPending(operation);
  }
  return operation->deferred.Promise();
}

void SmbConnectionWrap::OnReadComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data) {
  (void)smb2;
  (void)command_data;
  auto* operation = static_cast<ReadOperation*>(cb_data);
  SmbConnectionWrap* owner = operation->owner;
  Napi::HandleScope scope(owner->Env());

  if (operation->settled) {
    owner->FinishPending(operation);
    return;
  }
  if (status < 0) {
    const std::string message = owner->LastErrorMessage();
    const int nterror = owner->LastNtError();
    owner->RejectPending(operation, owner->ClassifyError(status, nterror, message), message, status, nterror);
    owner->FinishPending(operation);
    return;
  }

  operation->deferred.Resolve(Napi::Buffer<uint8_t>::Copy(owner->Env(), operation->buffer.data(), static_cast<size_t>(status)));
  operation->settled = true;
  owner->FinishPending(operation);
}

Napi::Value SmbConnectionWrap::Write(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 3 || !info[0].IsNumber() || !info[1].IsTypedArray() || !info[2].IsBigInt()) {
    Napi::TypeError::New(env, "write(handle, data, offset) requires number, Uint8Array, bigint").ThrowAsJavaScriptException();
    return env.Null();
  }

  const uint32_t handleId = info[0].As<Napi::Number>().Uint32Value();
  auto handleIt = file_handles_.find(handleId);
  if (handleIt == file_handles_.end()) {
    auto deferred = Napi::Promise::Deferred::New(env);
    deferred.Reject(CreateError("INVALID_STATE", "Unknown file handle", -EBADF, 0));
    return deferred.Promise();
  }

  bool lossless = false;
  const uint64_t offset = info[2].As<Napi::BigInt>().Uint64Value(&lossless);
  if (!lossless) {
    Napi::RangeError::New(env, "offset exceeds uint64 range").ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Uint8Array data = info[1].As<Napi::Uint8Array>();
  const uint32_t maxLength = smb2_get_max_write_size(context_);
  if (maxLength != 0 && data.ByteLength() > maxLength) {
    auto deferred = Napi::Promise::Deferred::New(env);
    deferred.Reject(CreateError("INVALID_STATE", "Requested write length exceeds server maximum", -EINVAL, 0));
    return deferred.Promise();
  }

  auto* operation = new WriteOperation(this);
  operation->buffer.assign(data.Data(), data.Data() + data.ByteLength());
  RegisterPending(operation);
  const int rc = smb2_pwrite_async(context_, handleIt->second.handle, operation->buffer.data(), static_cast<uint32_t>(operation->buffer.size()), offset, &SmbConnectionWrap::OnWriteComplete, operation);
  if (rc < 0) {
    const std::string message = LastErrorMessage();
    const int nterror = LastNtError();
    RejectPending(operation, ClassifyError(rc, nterror, message), message, rc, nterror, &handleIt->second.path);
    FinishPending(operation);
  }
  return operation->deferred.Promise();
}

void SmbConnectionWrap::OnWriteComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data) {
  (void)smb2;
  (void)command_data;
  auto* operation = static_cast<WriteOperation*>(cb_data);
  SmbConnectionWrap* owner = operation->owner;
  Napi::HandleScope scope(owner->Env());

  if (operation->settled) {
    owner->FinishPending(operation);
    return;
  }
  if (status < 0) {
    const std::string message = owner->LastErrorMessage();
    const int nterror = owner->LastNtError();
    owner->RejectPending(operation, owner->ClassifyError(status, nterror, message), message, status, nterror);
    owner->FinishPending(operation);
    return;
  }

  operation->deferred.Resolve(Napi::Number::New(owner->Env(), status));
  operation->settled = true;
  owner->FinishPending(operation);
}

Napi::Value SmbConnectionWrap::Ftruncate(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsBigInt()) {
    Napi::TypeError::New(env, "ftruncate(handle, length) requires number and bigint").ThrowAsJavaScriptException();
    return env.Null();
  }

  const uint32_t handleId = info[0].As<Napi::Number>().Uint32Value();
  auto handleIt = file_handles_.find(handleId);
  if (handleIt == file_handles_.end()) {
    auto deferred = Napi::Promise::Deferred::New(env);
    deferred.Reject(CreateError("INVALID_STATE", "Unknown file handle", -EBADF, 0));
    return deferred.Promise();
  }

  bool lossless = false;
  const uint64_t length = info[1].As<Napi::BigInt>().Uint64Value(&lossless);
  if (!lossless) {
    Napi::RangeError::New(env, "length exceeds uint64 range").ThrowAsJavaScriptException();
    return env.Null();
  }

  auto* operation = new HandleOperation(this, handleId);
  RegisterPending(operation);
  const int rc = smb2_ftruncate_async(context_, handleIt->second.handle, length, &SmbConnectionWrap::OnFtruncateComplete, operation);
  if (rc < 0) {
    const std::string message = LastErrorMessage();
    const int nterror = LastNtError();
    RejectPending(operation, ClassifyError(rc, nterror, message), message, rc, nterror, &handleIt->second.path);
    FinishPending(operation);
  }
  return operation->deferred.Promise();
}

void SmbConnectionWrap::OnFtruncateComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data) {
  (void)smb2;
  (void)command_data;
  auto* operation = static_cast<HandleOperation*>(cb_data);
  SmbConnectionWrap* owner = operation->owner;
  Napi::HandleScope scope(owner->Env());
  if (operation->settled) {
    owner->FinishPending(operation);
    return;
  }
  if (status < 0) {
    const std::string message = owner->LastErrorMessage();
    const int nterror = owner->LastNtError();
    owner->RejectPending(operation, owner->ClassifyError(status, nterror, message), message, status, nterror);
    owner->FinishPending(operation);
    return;
  }
  operation->deferred.Resolve(owner->Env().Undefined());
  operation->settled = true;
  owner->FinishPending(operation);
}

Napi::Value SmbConnectionWrap::Close(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsNumber()) {
    Napi::TypeError::New(env, "close(handle) requires a numeric handle").ThrowAsJavaScriptException();
    return env.Null();
  }

  const uint32_t handleId = info[0].As<Napi::Number>().Uint32Value();
  auto handleIt = file_handles_.find(handleId);
  if (handleIt == file_handles_.end()) {
    auto deferred = Napi::Promise::Deferred::New(env);
    deferred.Reject(CreateError("INVALID_STATE", "Unknown file handle", -EBADF, 0));
    return deferred.Promise();
  }

  smb2fh* handle = handleIt->second.handle;
  file_handles_.erase(handleIt);

  auto* operation = new HandleOperation(this, handleId);
  RegisterPending(operation);
  const int rc = smb2_close_async(context_, handle, &SmbConnectionWrap::OnCloseComplete, operation);
  if (rc < 0) {
    const std::string message = LastErrorMessage();
    const int nterror = LastNtError();
    RejectPending(operation, ClassifyError(rc, nterror, message), message, rc, nterror);
    FinishPending(operation);
  }
  return operation->deferred.Promise();
}

void SmbConnectionWrap::OnCloseComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data) {
  (void)smb2;
  (void)command_data;
  auto* operation = static_cast<HandleOperation*>(cb_data);
  SmbConnectionWrap* owner = operation->owner;
  Napi::HandleScope scope(owner->Env());
  if (operation->settled) {
    owner->FinishPending(operation);
    return;
  }
  if (status < 0) {
    const std::string message = owner->LastErrorMessage();
    const int nterror = owner->LastNtError();
    owner->RejectPending(operation, owner->ClassifyError(status, nterror, message), message, status, nterror);
  } else {
    operation->deferred.Resolve(owner->Env().Undefined());
    operation->settled = true;
  }
  owner->FinishPending(operation);
}

Napi::Value SmbConnectionWrap::Mkdir(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "mkdir(path) requires a string path").ThrowAsJavaScriptException();
    return env.Null();
  }
  auto* operation = new PendingOperation(this);
  operation->path = info[0].As<Napi::String>().Utf8Value();
  RegisterPending(operation);
  const int rc = smb2_mkdir_async(context_, operation->path.c_str(), &SmbConnectionWrap::OnPathComplete, operation);
  if (rc < 0) {
    const std::string message = LastErrorMessage();
    const int nterror = LastNtError();
    RejectPending(operation, ClassifyError(rc, nterror, message), message, rc, nterror, &operation->path);
    FinishPending(operation);
  }
  return operation->deferred.Promise();
}

Napi::Value SmbConnectionWrap::Rmdir(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "rmdir(path) requires a string path").ThrowAsJavaScriptException();
    return env.Null();
  }
  auto* operation = new PendingOperation(this);
  operation->path = info[0].As<Napi::String>().Utf8Value();
  RegisterPending(operation);
  const int rc = smb2_rmdir_async(context_, operation->path.c_str(), &SmbConnectionWrap::OnPathComplete, operation);
  if (rc < 0) {
    const std::string message = LastErrorMessage();
    const int nterror = LastNtError();
    RejectPending(operation, ClassifyError(rc, nterror, message), message, rc, nterror, &operation->path);
    FinishPending(operation);
  }
  return operation->deferred.Promise();
}

Napi::Value SmbConnectionWrap::Unlink(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "unlink(path) requires a string path").ThrowAsJavaScriptException();
    return env.Null();
  }
  auto* operation = new PendingOperation(this);
  operation->path = info[0].As<Napi::String>().Utf8Value();
  RegisterPending(operation);
  const int rc = smb2_unlink_async(context_, operation->path.c_str(), &SmbConnectionWrap::OnPathComplete, operation);
  if (rc < 0) {
    const std::string message = LastErrorMessage();
    const int nterror = LastNtError();
    RejectPending(operation, ClassifyError(rc, nterror, message), message, rc, nterror, &operation->path);
    FinishPending(operation);
  }
  return operation->deferred.Promise();
}

void SmbConnectionWrap::OnPathComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data) {
  (void)smb2;
  (void)command_data;
  auto* operation = static_cast<PendingOperation*>(cb_data);
  SmbConnectionWrap* owner = operation->owner;
  Napi::HandleScope scope(owner->Env());
  if (operation->settled) {
    owner->FinishPending(operation);
    return;
  }
  if (status < 0) {
    const std::string message = owner->LastErrorMessage();
    const int nterror = owner->LastNtError();
    owner->RejectPending(operation, owner->ClassifyError(status, nterror, message), message, status, nterror, &operation->path);
  } else {
    operation->deferred.Resolve(owner->Env().Undefined());
    operation->settled = true;
  }
  owner->FinishPending(operation);
}

Napi::Value SmbConnectionWrap::Rename(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2 || !info[0].IsString() || !info[1].IsString()) {
    Napi::TypeError::New(env, "rename(oldPath, newPath) requires two string paths").ThrowAsJavaScriptException();
    return env.Null();
  }
  auto* operation = new RenameOperation(this);
  operation->path = info[0].As<Napi::String>().Utf8Value();
  operation->new_path = info[1].As<Napi::String>().Utf8Value();
  RegisterPending(operation);
  const int rc = smb2_rename_async(context_, operation->path.c_str(), operation->new_path.c_str(), &SmbConnectionWrap::OnRenameComplete, operation);
  if (rc < 0) {
    const std::string message = LastErrorMessage();
    const int nterror = LastNtError();
    RejectPending(operation, ClassifyError(rc, nterror, message), message, rc, nterror, &operation->path);
    FinishPending(operation);
  }
  return operation->deferred.Promise();
}

void SmbConnectionWrap::OnRenameComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data) {
  (void)smb2;
  (void)command_data;
  auto* operation = static_cast<RenameOperation*>(cb_data);
  SmbConnectionWrap* owner = operation->owner;
  Napi::HandleScope scope(owner->Env());
  if (operation->settled) {
    owner->FinishPending(operation);
    return;
  }
  if (status < 0) {
    const std::string message = owner->LastErrorMessage();
    const int nterror = owner->LastNtError();
    owner->RejectPending(operation, owner->ClassifyError(status, nterror, message), message, status, nterror, &operation->path);
  } else {
    operation->deferred.Resolve(owner->Env().Undefined());
    operation->settled = true;
  }
  owner->FinishPending(operation);
}

Napi::Value SmbConnectionWrap::Opendir(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "opendir(path) requires a string path").ThrowAsJavaScriptException();
    return env.Null();
  }
  auto* operation = new OpenOperation(this);
  operation->path = info[0].As<Napi::String>().Utf8Value();
  RegisterPending(operation);
  const int rc = smb2_opendir_async(context_, operation->path.c_str(), &SmbConnectionWrap::OnOpendirComplete, operation);
  if (rc < 0) {
    const std::string message = LastErrorMessage();
    const int nterror = LastNtError();
    RejectPending(operation, ClassifyError(rc, nterror, message), message, rc, nterror, &operation->path);
    FinishPending(operation);
  }
  return operation->deferred.Promise();
}

void SmbConnectionWrap::OnOpendirComplete(struct smb2_context* smb2, int status, void* command_data, void* cb_data) {
  (void)smb2;
  auto* operation = static_cast<OpenOperation*>(cb_data);
  SmbConnectionWrap* owner = operation->owner;
  Napi::HandleScope scope(owner->Env());
  auto* handle = static_cast<smb2dir*>(command_data);

  if (operation->settled || owner->disconnecting_) {
    if (status == 0 && handle != nullptr && owner->context_ != nullptr) {
      smb2_closedir(owner->context_, handle);
    }
    if (!operation->settled) {
      owner->RejectPending(operation, "CONNECTION", "Connection closed", -ECONNRESET, 0, &operation->path);
    }
    owner->FinishPending(operation);
    return;
  }

  if (status < 0) {
    const std::string message = owner->LastErrorMessage();
    const int nterror = owner->LastNtError();
    owner->RejectPending(operation, owner->ClassifyError(status, nterror, message), message, status, nterror, &operation->path);
    owner->FinishPending(operation);
    return;
  }

  const uint32_t handleId = owner->next_dir_handle_++;
  owner->dir_handles_.emplace(handleId, DirHandleEntry{handle, operation->path});
  operation->deferred.Resolve(Napi::Number::New(owner->Env(), handleId));
  operation->settled = true;
  owner->FinishPending(operation);
}

Napi::Value SmbConnectionWrap::Readdir(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsNumber()) {
    Napi::TypeError::New(env, "readdir(handle) requires a numeric handle").ThrowAsJavaScriptException();
    return env.Null();
  }
  auto deferred = Napi::Promise::Deferred::New(env);
  const uint32_t handleId = info[0].As<Napi::Number>().Uint32Value();
  auto handleIt = dir_handles_.find(handleId);
  if (handleIt == dir_handles_.end()) {
    deferred.Reject(CreateError("INVALID_STATE", "Unknown directory handle", -EBADF, 0));
    return deferred.Promise();
  }

  smb2dirent* entry = smb2_readdir(context_, handleIt->second.handle);
  if (entry == nullptr) {
    deferred.Resolve(env.Null());
    return deferred.Promise();
  }

  const std::string fullPath = handleIt->second.path.empty() ? std::string(entry->name) : handleIt->second.path + "\\" + entry->name;
  deferred.Resolve(StatToJs(entry->st, fullPath, entry->name));
  return deferred.Promise();
}

Napi::Value SmbConnectionWrap::Closedir(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsNumber()) {
    Napi::TypeError::New(env, "closedir(handle) requires a numeric handle").ThrowAsJavaScriptException();
    return env.Null();
  }
  auto deferred = Napi::Promise::Deferred::New(env);
  const uint32_t handleId = info[0].As<Napi::Number>().Uint32Value();
  auto handleIt = dir_handles_.find(handleId);
  if (handleIt == dir_handles_.end()) {
    deferred.Reject(CreateError("INVALID_STATE", "Unknown directory handle", -EBADF, 0));
    return deferred.Promise();
  }

  smb2_closedir(context_, handleIt->second.handle);
  dir_handles_.erase(handleIt);
  deferred.Resolve(env.Undefined());
  return deferred.Promise();
}

Napi::Value SmbConnectionWrap::GetMaxReadSize(const Napi::CallbackInfo& info) {
  (void)info;
  return Napi::Number::New(Env(), context_ == nullptr ? 0 : smb2_get_max_read_size(context_));
}

Napi::Value SmbConnectionWrap::GetMaxWriteSize(const Napi::CallbackInfo& info) {
  (void)info;
  return Napi::Number::New(Env(), context_ == nullptr ? 0 : smb2_get_max_write_size(context_));
}

Napi::Value SmbConnectionWrap::GetConnected(const Napi::CallbackInfo& info) {
  (void)info;
  return Napi::Boolean::New(Env(), connected_ && !disconnecting_);
}
