import { execFileSync } from "node:child_process";

function resolveNpmCommand(args) {
  if (process.platform === "win32") {
    return {
      command: "cmd",
      args: ["/c", "npm.cmd", ...args]
    };
  }

  return {
    command: "npm",
    args
  };
}

export function runNpm(args, options = {}) {
  const { command, args: resolvedArgs } = resolveNpmCommand(args);
  execFileSync(command, resolvedArgs, {
    stdio: "inherit",
    ...options
  });
}
