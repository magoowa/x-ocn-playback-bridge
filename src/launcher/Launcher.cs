using System;
using System.Management.Automation;
using System.Management.Automation.Runspaces;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Windows.Forms;

[assembly: AssemblyTitle("X-OCN Playback Bridge Setup")]
[assembly: AssemblyCompany("magoowa")]
[assembly: AssemblyProduct("X-OCN Playback Bridge")]
[assembly: AssemblyCopyright("magoowa")]
[assembly: AssemblyInformationalVersion("1.23.2026.1115")]
[assembly: AssemblyVersion("1.23.2026.1115")]
[assembly: AssemblyFileVersion("1.23.2026.1115")]

public static class SetupDisplay {
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    public static extern uint GetDpiForWindow(IntPtr window);
}

internal static class Program {
    [STAThread]
    static int Main() {
        string directory = Path.Combine(Path.GetTempPath(), "XOCN-Playback-Bridge-" + Guid.NewGuid().ToString("N"));
        try {
            Directory.CreateDirectory(directory);
            string archive = Path.Combine(directory, "payload.zip");
            using (Stream input = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip"))
            using (FileStream output = File.Create(archive)) {
                if (input == null) throw new InvalidOperationException("The installer payload is missing.");
                input.CopyTo(output);
            }
            string content = Path.Combine(directory, "content");
            ZipFile.ExtractToDirectory(archive, content);
            Environment.SetEnvironmentVariable("XOCN_SETUP_EXECUTABLE", Assembly.GetExecutingAssembly().Location);
            using (var runspace = RunspaceFactory.CreateRunspace()) {
                runspace.ThreadOptions = PSThreadOptions.UseCurrentThread;
                runspace.Open();
                using (var setup = PowerShell.Create()) {
                    setup.Runspace = runspace;
                    setup.AddCommand(Path.Combine(content, "Setup.ps1"));
                    setup.Invoke();
                    if (setup.Streams.Error.Count != 0)
                        throw new InvalidOperationException(setup.Streams.Error[0].ToString());
                    if (setup.InvocationStateInfo.State == PSInvocationState.Failed)
                        throw new InvalidOperationException("Setup failed.", setup.InvocationStateInfo.Reason);
                }
            }
            return 0;
        } catch (Exception e) {
            MessageBox.Show(e.Message, "X-OCN Playback Bridge", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        } finally {
            try { if (Directory.Exists(directory)) Directory.Delete(directory, true); } catch { }
        }
    }
}
