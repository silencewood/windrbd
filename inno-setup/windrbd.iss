; Script generated by the Inno Setup Script Wizard.
; SEE THE DOCUMENTATION FOR DETAILS ON CREATING INNO SETUP SCRIPT FILES!
; Use iscc to compile this (in your innosetup program directory)

; Autogenerated by versioninfo.sh: contains MyAppVersion
#include "version.iss"

; Windows-style path to the root of the source distribution
; You probably have to change this.
#define WindrbdSource "Y:\windrbd"
; Where the WinDRBD utils (drbdadm, drbdsetup, drbdmeta and windrbd EXEs)
; can be found. Note: the utils are not built by this makefile, you
; have to build them seperately.
#define WindrbdUtilsSource "Y:\drbd-utils-windows"

#define MyAppName "WinDRBD"
#define MyAppPublisher "Linbit"
#define MyAppURL "http://www.linbit.com/"

[Setup]
; NOTE: The value of AppId uniquely identifies this application.
; Do not use the same AppId value in installers for other applications.
; (To generate a new GUID, click Tools | Generate GUID inside the IDE.)
AppId={{EB75FCBA-83D5-4DBF-9047-30F2B6C72DC9}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
;AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
ChangesEnvironment=yes
DefaultDirName={pf}\{#MyAppName}
DisableDirPage=yes
DefaultGroupName={#MyAppName}
LicenseFile={#WindrbdSource}\LICENSE.txt
InfoBeforeFile={#WindrbdSource}\WinDRBDReleaseDescriptionBeta4.txt
OutputDir={#WindrbdSource}\inno-setup
OutputBaseFilename=install-{#MyAppVersion}
PrivilegesRequired=admin
Compression=lzma
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "catalan"; MessagesFile: "compiler:Languages\Catalan.isl"
Name: "corsican"; MessagesFile: "compiler:Languages\Corsican.isl"
Name: "czech"; MessagesFile: "compiler:Languages\Czech.isl"
Name: "danish"; MessagesFile: "compiler:Languages\Danish.isl"
Name: "dutch"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "finnish"; MessagesFile: "compiler:Languages\Finnish.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "greek"; MessagesFile: "compiler:Languages\Greek.isl"
Name: "hebrew"; MessagesFile: "compiler:Languages\Hebrew.isl"
Name: "hungarian"; MessagesFile: "compiler:Languages\Hungarian.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "norwegian"; MessagesFile: "compiler:Languages\Norwegian.isl"
Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "portuguese"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "scottishgaelic"; MessagesFile: "compiler:Languages\ScottishGaelic.isl"
Name: "serbiancyrillic"; MessagesFile: "compiler:Languages\SerbianCyrillic.isl"
Name: "serbianlatin"; MessagesFile: "compiler:Languages\SerbianLatin.isl"
Name: "slovenian"; MessagesFile: "compiler:Languages\Slovenian.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"

[Tasks]
; Name: modifypath; Description: &Add application directory to your environmental path;

[Files]
Source: "{#WindrbdUtilsSource}\user\v9\drbdadm.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdUtilsSource}\user\v9\drbdmeta.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdUtilsSource}\user\v9\drbdsetup.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdUtilsSource}\user\windrbd\windrbd.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\uninstall-windrbd-beta4.cmd"; DestDir: "{app}"; Flags: ignoreversion deleteafterinstall
Source: "{#WindrbdSource}\inno-setup\install-windrbd.cmd"; DestDir: "{app}"; Flags: ignoreversion deleteafterinstall
Source: "{#WindrbdSource}\inno-setup\uninstall-windrbd.cmd"; DestDir: "{app}"; Flags: ignoreversion
; TODO: one day, package the individual files themselves:
Source: "{#WindrbdSource}\inno-setup\sysroot.zip"; DestDir: "{app}"; Flags: ignoreversion deleteafterinstall
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\cygwin1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\cygbz2-1.dll"; DestDir: "{app}"; Flags: ignoreversion deleteafterinstall
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\unzip.exe"; DestDir: "{app}"; Flags: ignoreversion deleteafterinstall
Source: "{#WindrbdSource}\converted-sources\drbd\windrbd.sys"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\msgbox.vbs"; DestDir: "{app}"; Flags: ignoreversion deleteafterinstall
Source: "{#WindrbdSource}\converted-sources\drbd\windrbd.inf"; DestDir: "{app}"; Flags: ignoreversion

; NOTE: Don't use "Flags: ignoreversion" on any shared system files

; [Registry]
; Root: HKLM; Subkey: "System\CurrentControlSet\services\WinDRBD"; Flags: uninsdeletekey
; Root: HKLM; Subkey: "System\CurrentControlSet\services\WinDRBD"; Flags: uninsdeletevalue; ValueType: string; ValueName: "Description"; ValueData: "WinDRBD - storage replication over network for Microsoft Windows"
; Root: HKLM; Subkey: "System\CurrentControlSet\services\WinDRBD"; Flags: uninsdeletevalue; ValueType: dword; ValueName: "Type"; ValueData: 1
; Root: HKLM; Subkey: "System\CurrentControlSet\services\WinDRBD"; Flags: uninsdeletevalue; ValueType: dword; ValueName: "Start"; ValueData: 3
; Root: HKLM; Subkey: "System\CurrentControlSet\services\WinDRBD"; Flags: uninsdeletevalue; ValueType: dword; ValueName: "ErrorControl"; ValueData: 0
; Root: HKLM; Subkey: "System\CurrentControlSet\services\WinDRBD"; Flags: uninsdeletevalue; ValueType: expandsz; ValueName: "ImagePath"; ValueData: "system32\drivers\windrbd.sys"
; Root: HKLM; Subkey: "System\CurrentControlSet\services\WinDRBD"; Flags: uninsdeletevalue; ValueType: string; ValueName: "DisplayName"; ValueData: "WinDRBD"

[Icons]
Name: "{group}\{cm:ProgramOnTheWeb,{#MyAppName}}"; Filename: "{#MyAppURL}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
                                                
[Run]
Filename: "{app}\uninstall-windrbd-beta4.cmd"; WorkingDir: "{app}"; Flags: runascurrentuser shellexec waituntilterminated runhidden
Filename: "{app}\install-windrbd.cmd"; WorkingDir: "{app}"; Flags: runascurrentuser shellexec waituntilterminated runhidden

[UninstallRun]
Filename: "{app}\uninstall-windrbd.cmd"; WorkingDir: "{app}"; Flags: runascurrentuser shellexec waituntilterminated runhidden

[Code]
const
	ModPathType = 'system';

	function ModPathDir(): TArrayOfString;
	begin
		setArrayLength(Result, 1);
		Result[0] := ExpandConstant('{app}');
	end;

#include "modpath.iss"

function UninstallNeedRestart(): Boolean;
begin
	Result := True;
end;