// @category YPHZ.RE
// Pre-analysis hook: enable the Function ID analyzer and load installed FID
// databases before auto-analysis.  Ghidra 12 ships packed .fidbf libraries
// under Ghidra/Features/FunctionID/data; they are for 32/64-bit Visual Studio
// code, so a 16-bit DOS program may legitimately report canQuery=false.
import ghidra.app.script.GhidraScript;
import ghidra.feature.fid.db.FidFile;
import ghidra.feature.fid.db.FidFileManager;
import ghidra.feature.fid.service.FidService;

import java.util.Map;

public class EnableFid extends GhidraScript {
    @Override
    public void run() throws Exception {
        Map<String, String> options =
                getCurrentAnalysisOptionsAndValues(currentProgram);
        if (options.containsKey("Function ID")) {
            setAnalysisOption(currentProgram, "Function ID", "true");
            println("[enable-fid] Function ID option=true");
        }
        else {
            println("[enable-fid] Function ID option not registered");
        }

        FidFileManager manager = FidFileManager.getInstance();
        manager.load();
        int active = 0;
        for (FidFile f : manager.getFidFiles()) {
            if (f.isActive()) {
                active++;
            }
        }
        FidService service = new FidService();
        println("[enable-fid] fid_files=" + manager.getFidFiles().size()
                + " active=" + active
                + " canProcessLanguage=" + service.canProcess(
                        currentProgram.getLanguage())
                + " language=" + currentProgram.getLanguageID());
    }
}
