using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Windows.Forms;

[assembly: AssemblyTitle("X-OCN Playback Bridge Setup")]
[assembly: AssemblyCompany("magoowa")]
[assembly: AssemblyProduct("X-OCN Playback Bridge")]
[assembly: AssemblyCopyright("magoowa")]
[assembly: AssemblyVersion("1.13.2026.1114")]
[assembly: AssemblyFileVersion("1.13.2026.1114")]

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
            string powershell = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), @"WindowsPowerShell\v1.0\powershell.exe");
            var start = new ProcessStartInfo {
                FileName = powershell,
                Arguments = "-NoProfile -ExecutionPolicy Bypass -File \"" + Path.Combine(content, "Setup.ps1") + "\"",
                UseShellExecute = false,
                CreateNoWindow = true,
                WindowStyle = ProcessWindowStyle.Hidden,
                WorkingDirectory = content
            };
            start.EnvironmentVariables["XOCN_SETUP_EXECUTABLE"] = Assembly.GetExecutingAssembly().Location;
            using (Process process = Process.Start(start)) {
                process.WaitForExit();
                if (process.ExitCode != 0) throw new InvalidOperationException("Setup could not start (error " + process.ExitCode + ").");
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
