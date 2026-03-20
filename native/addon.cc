#include <napi.h>
#include <fcntl.h>

#include "smb_connection.h"

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  SmbConnectionWrap::Init(env, exports);

  Napi::Object constants = Napi::Object::New(env);
  constants.Set("O_RDONLY", Napi::Number::New(env, O_RDONLY));
  constants.Set("O_WRONLY", Napi::Number::New(env, O_WRONLY));
  constants.Set("O_RDWR", Napi::Number::New(env, O_RDWR));
  constants.Set("O_CREAT", Napi::Number::New(env, O_CREAT));
  constants.Set("O_EXCL", Napi::Number::New(env, O_EXCL));
  constants.Set("O_TRUNC", Napi::Number::New(env, O_TRUNC));
  exports.Set("constants", constants);
  return exports;
}

NODE_API_MODULE(smbx, InitAll)
