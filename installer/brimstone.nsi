; Brimstone Maximizer - Windows installer (NSIS 3)
; Copyright (C) 2026 Brimstone authors - SPDX-License-Identifier: GPL-2.0-or-later
; Build: makensis installer/brimstone.nsi  (from ../build.sh, after the Windows build)

Unicode true
!include "MUI2.nsh"
!include "x64.nsh"

!define NAME     "Brimstone Maximizer"
!define VERSION  "1.1.0"
!define BUNDLE   "brimstone_maximizer"
!define MANUAL   "Brimstone-Maximizer-manuel.pdf"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\BrimstoneMaximizer"

Name "${NAME} ${VERSION}"
OutFile "../bin/Brimstone-Maximizer-${VERSION}-setup.exe"
RequestExecutionLevel admin
InstallDir "$COMMONFILES64\VST3"
SetCompressor /SOLID lzma

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_LICENSE "../COPYING"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "French"

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "${NAME} needs 64-bit Windows."
        Abort
    ${EndIf}
    SetRegView 64
FunctionEnd

Function un.onInit
    SetRegView 64
    SetShellVarContext all
FunctionEnd

Section "VST3" SecVST3
    SectionIn RO
    SetOutPath "$COMMONFILES64\VST3"
    File /r "../bin/windows/${BUNDLE}.vst3"
    SetOutPath "$PROGRAMFILES64\Brimstone"
    File "../COPYING"
    WriteUninstaller "$PROGRAMFILES64\Brimstone\uninstall.exe"
    WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "${NAME}"
    WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${UNINSTKEY}" "Publisher" "Brimstone"
    WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" '"$PROGRAMFILES64\Brimstone\uninstall.exe"'
    WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1
SectionEnd

Section "CLAP" SecCLAP
    SetOutPath "$COMMONFILES64\CLAP"
    File "../bin/windows/${BUNDLE}.clap"
SectionEnd

; user manual (French, PDF) next to the licence, with a Start menu shortcut for all users
Section "Manual (PDF)" SecManual
    SetShellVarContext all
    SetOutPath "$PROGRAMFILES64\Brimstone"
    File "/oname=${MANUAL}" "../doc/manuel.pdf"
    CreateDirectory "$SMPROGRAMS\${NAME}"
    CreateShortcut "$SMPROGRAMS\${NAME}\${NAME} - manuel.lnk" "$PROGRAMFILES64\Brimstone\${MANUAL}"
SectionEnd

Section "Uninstall"
    SetShellVarContext all   ; the shortcut is in the Start menu of all users (NSIS default: current user)
    Delete "$SMPROGRAMS\${NAME}\${NAME} - manuel.lnk"
    RMDir "$SMPROGRAMS\${NAME}"
    Delete "$PROGRAMFILES64\Brimstone\${MANUAL}"
    RMDir /r "$COMMONFILES64\VST3\${BUNDLE}.vst3"
    Delete "$COMMONFILES64\CLAP\${BUNDLE}.clap"
    Delete "$PROGRAMFILES64\Brimstone\COPYING"
    Delete "$PROGRAMFILES64\Brimstone\uninstall.exe"
    RMDir "$PROGRAMFILES64\Brimstone"
    DeleteRegKey HKLM "${UNINSTKEY}"
SectionEnd
