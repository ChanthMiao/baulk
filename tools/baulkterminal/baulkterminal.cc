//
#include <bela/base.hpp>
#include <bela/path.hpp>
#include <bela/escapeargv.hpp>
#include <bela/parseargv.hpp>
#include <bela/picker.hpp>
#include <bela/str_replace.hpp>
#include <shellapi.h>
#include <version.hpp>
#include "baulkterminal.hpp"

namespace baulkterminal {

bool IsDebugMode = false;

void BaulkMessage() {
  constexpr wchar_t usage[] = LR"(baulkterminal - Baulk Terminal Launcher
Usage: baulkterminal [option] ...
  -h|--help
               Show usage text and quit
  -v|--version
               Show version number and quit
  -V|--verbose
               Make the operation more talkative
  -C|--cleanup
               Create clean environment variables to avoid interference
  -S|--shell
               The shell you want to start. allowed: pwsh, bash, cmd, wsl
  -W|--cwd
               Set the shell startup directory
  -A|--arch
               Select a specific arch, use native architecture by default
  -E|--venv
               Choose to load one/more specific package virtual environment
  --vs
               Load Visual Studio related environment variables
  --vs-preview
               Load Visual Studio (Preview) related environment variables
  --conhost
               Use conhost not Windows terminal

)";
  bela::BelaMessageBox(nullptr, L"Baulk Terminal Launcher", usage, BAULK_APPLINK, bela::mbs_t::ABOUT);
}

bool Executor::ParseArgv(bela::error_code &ec) {
  int Argc = 0;
  auto Argv = CommandLineToArgvW(GetCommandLineW(), &Argc);
  if (Argv == nullptr) {
    ec = bela::make_system_error_code();
    return false;
  }
  auto closer = bela::finally([&] { LocalFree(Argv); });
  bela::ParseArgv pa(Argc, Argv);
  pa.Add(L"help", bela::no_argument, L'h')
      .Add(L"version", bela::no_argument, L'v')
      .Add(L"cleanup", bela::no_argument, L'C') // cleanup environment
      .Add(L"verbose", bela::no_argument, L'V')
      .Add(L"shell", bela::required_argument, L'S')
      .Add(L"cwd", bela::required_argument, L'W')
      .Add(L"arch", bela::required_argument, L'A')
      .Add(L"venv", bela::required_argument, L'E') // virtual env support
      .Add(L"vs", bela::no_argument, 1000)         // load visual studio environment
      .Add(L"vs-preview", bela::no_argument, 1001) // load visual studio (Preview) environment
      .Add(L"conhost", bela::no_argument, 1002);   // disable windows termainl
  auto ret = pa.Execute(
      [this](int val, const wchar_t *oa, const wchar_t *) {
        switch (val) {
        case 'h':
          BaulkMessage();
          ExitProcess(0);
        case 'v':
          bela::BelaMessageBox(nullptr, L"Baulk Terminal Launcher", BAULK_APPVERSION, BAULK_APPLINK,
                               bela::mbs_t::ABOUT);
          ExitProcess(0);
        case 'C':
          cleanup = true;
          break;
        case 'V':
          IsDebugMode = true;
          break;
        case 'S':
          shell = oa;
          break;
        case 'W':
          cwd = oa;
          break;
        case 'A': {
          auto larch = bela::AsciiStrToLower(oa);
          if (larch == L"x64" || larch == L"arm64" || larch == L"x86" || larch == L"arm") {
            arch = larch;
            break;
          }
          auto msg = bela::StringCat(L"Invalid arch: ", oa);
          bela::BelaMessageBox(nullptr, L"Baulk Terminal Launcher", msg.data(), BAULK_APPLINKE, bela::mbs_t::FATAL);
        } break;
        case 'E':
          venvs.push_back(oa);
          break;
        case 1000:
          if (!usevspreview) {
            usevs = true;
          }
          break;
        case 1001:
          if (!usevs) {
            usevspreview = true;
          }
          break;
        case 1002:
          conhost = true;
          break;
          break;
        default:
          break;
        }
        return true;
      },
      ec);
  if (!ret) {
    return false;
  }
  // CommandLineToArgvW BUG
  if (cwd.size() == 3 && isalpha(cwd[0]) && cwd[1] == L':' && cwd[2] == '"') {
    cwd.back() = L'\\';
  }
  return true;
}
} // namespace baulkterminal

class dotcom_global_initializer {
public:
  dotcom_global_initializer() {
    auto hr = CoInitialize(NULL);
    if (FAILED(hr)) {
      auto ec = bela::make_system_error_code();
      MessageBoxW(nullptr, ec.data(), L"CoInitialize", IDOK);
      exit(1);
    }
  }
  ~dotcom_global_initializer() { CoUninitialize(); }
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
  dotcom_global_initializer di;
  baulkterminal::Executor executor;
  bela::error_code ec;
  if (!executor.ParseArgv(ec)) {
    bela::BelaMessageBox(nullptr, L"BaulkTerminal: Parse Argv error", ec.data(), BAULK_APPLINKE, bela::mbs_t::FATAL);
    return 1;
  }
  bela::EscapeArgv ea;
  if (!executor.PrepareArgv(ea, ec)) {
    bela::BelaMessageBox(nullptr, L"BaulkTerminal: Prepare Argv error", ec.data(), BAULK_APPLINKE, bela::mbs_t::FATAL);
    return 1;
  }
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  SecureZeroMemory(&si, sizeof(si));
  SecureZeroMemory(&pi, sizeof(pi));
  si.cb = sizeof(si);
  baulkterminal::DbgPrint(L"commandline: %s", ea.sv());
  if (CreateProcessW(nullptr, ea.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &si,
                     &pi) != TRUE) {
    auto ec = bela::make_system_error_code();
    bela::BelaMessageBox(nullptr, L"unable open Windows Terminal", ec.data(), nullptr, bela::mbs_t::FATAL);
    return -1;
  }
  return 0;
}
