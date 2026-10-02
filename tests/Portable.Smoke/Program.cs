using System.Diagnostics;
using System.IO;
using System.Text.Json;
using Csnz.Launcher;
var directory = Path.GetFullPath(args[0]);
if (!directory.Contains("portable-smoke-")) throw new Exception("Test directory must be isolated.");
Directory.CreateDirectory(directory);
string NewRoot(string name) {var root = Path.Combine(directory, name);Directory.CreateDirectory(Path.Combine(root,"Bin"));File.WriteAllText(Path.Combine(root,"Bin","CSOLauncher.exe"),"fixture");return root;}
var a = NewRoot("游戏 A" ); var b = NewRoot("moved game B");var detached=Path.Combine(directory,"detached");Directory.CreateDirectory(detached);
int count=0;
void Check(bool valid,string name) {if(!valid)throw new Exception("FAIL: "+name);Console.WriteLine("PASS: "+name);count++;}
Check(GamePaths.ResolveRoot("",false,a)==a,"fresh install discovers EXE-adjacent game");
Check(GamePaths.ResolveRoot(a,false,b)==b,"relocation prefers adjacent game over old still-valid path");
Check(GamePaths.ResolveRoot(a,true,b)==a,"explicit external path remains explicit");
Check(GamePaths.ResolveRoot(Path.Combine(directory,"missing"),true,b)==b,"stale manual path recovers beside EXE");
Check(GamePaths.ResolveRoot(a,false,detached)==a,"detached launcher retains configured game");
Check(GamePaths.ResolveRoot("",false,detached)=="","no hardcoded machine path or CWD fallback");
Check(GamePaths.ResolveRoot("",false,Path.Combine(b,"Bin"))==b,"EXE inside Bin resolves game parent");
Check(GamePaths.NormalizeRoot(Path.Combine(a,"Bin","CSOLauncher.exe"))==a,"game executable path normalizes");
var s=new LauncherSettings{GameRoot=a,Account="qauser1",ProtectedPassword="unchanged-ciphertext",RememberPassword=true};
var binding=s.CredentialBinding;
SettingsStore.ResolveGameRoot(s,b);
Check(s.ProtectedPassword=="unchanged-ciphertext" && s.CredentialBinding==binding && s.RememberPassword,"path detection preserves saved credential binding");
var start=GameLauncher.CreateStartInfo(s,"qauser1","@Test-pass!");
Check(!start.ArgumentList.Contains("-username") && !start.ArgumentList.Contains("-password") && !start.ArgumentList.Contains("@Test-pass!"),"credentials never enter game command line");
Check(start.WorkingDirectory==Path.Combine(b,"Bin") && start.ArgumentList.Contains("-disableauthui"),"child workdir and no duplicate login UI");
Check(ProcessTools.GetImagePath(Environment.ProcessId)==Environment.ProcessPath,"x86 query resolves own process");
using var child=Process.Start(new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows),"Sysnative","WindowsPowerShell","v1.0","powershell.exe")){UseShellExecute=false,CreateNoWindow=true,ArgumentList={"-NoProfile","-Command","Start-Sleep -Seconds 10"}})!;
try {Check(ProcessTools.GetImagePath(child.Id).EndsWith("powershell.exe",StringComparison.OrdinalIgnoreCase),"x86 launcher queries 64-bit process path");}
finally {if(!child.HasExited){child.Kill();child.WaitForExit();}}
Console.WriteLine($"PORTABLE PASS: {count}");
