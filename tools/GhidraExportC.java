// @category YPHZ.RE
// Ghidra headless GhidraScript: export one C file per function and index.json.
//
// Run with:
//   YPHZ_RE_OUT=/abs/re_out analyzeHeadless ... \
//       -postScript GhidraExportC.java -scriptPath /tmp/yphz_ghidra_scripts
//
// Java is the native GhidraScript language in Ghidra 12; unlike the optional
// Jython provider it never needs a Python runtime and never fails on a
// non-ASCII script path.  The companion ghidra_export_c.py documents the
// equivalent Jython object model and can be used to enrich index.json on
// older Ghidra installations.

import java.io.File;
import java.io.FileWriter;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryAccessException;
import ghidra.program.model.scalar.Scalar;
import ghidra.feature.fid.db.FidFileManager;
import ghidra.feature.fid.db.FidQueryService;
import ghidra.feature.fid.service.FidMatch;
import ghidra.feature.fid.service.FidSearchResult;
import ghidra.feature.fid.service.FidService;
import ghidra.util.task.ConsoleTaskMonitor;
import ghidra.util.task.TaskMonitor;

public class GhidraExportC extends GhidraScript {
    private static final String[] ASSET_NAMES = {
        "SNDATA.GP", "SAVEDATA.GP", "MAINCMD.GP", "MAINCMD2.GP", "MAINOBJ.GP",
        "MAIN.EXE", "MAINEVT.GP", "MAINITEM.GP", "MAINANM.GP", "MAINMAP.GP",
        "MAINPAL.PLD", "MESSAGE.GP", "MUSIC.GP", "PCM.GP", "HAIKEI.GP",
        "KISETSU.GP", "HCHIKEI.GP", "HKESHIKI.GP", "HGROUND.GP", "HUNITPAT.GP",
        "HIKUCHI.GP", "HKUMI.GP", "HIKBACK.GP", "HKEI.GP", "LOSEPAL.PLD",
        "KAODATA.GP", "MONTAGE.GP", "LOGO.GP", "DISK-1.GP", "DISK-2.GP",
        "DISK-3.GP", "DISK-4.GP", "DISK-5.GP", "GEN.DIR"
    };

    private final Charset sjis = Charset.forName("windows-31j");
    private Memory memory;
    private Listing listing;
    private TaskMonitor monitor;
    private Map<String, List<Address>> assetAddresses;
    private Set<String> assetNames;
    private Map<String, String> fidMatches;
    private boolean fidAvailable;
    private String fidStatus = "";
    private int candidateAssetRefs;
    private int validatedAssetRefs;

    @Override
    public void run() throws Exception {
        String outRoot = System.getenv("YPHZ_RE_OUT");
        if (outRoot == null || outRoot.length() == 0) {
            outRoot = "/tmp/yphz_re_out";
        }
        String binName = currentProgram.getName();
        File outDir = new File(new File(outRoot, "decompiled"), binName);
        if (!outDir.isDirectory() && !outDir.mkdirs()) {
            throw new RuntimeException("cannot create " + outDir);
        }

        memory = currentProgram.getMemory();
        listing = currentProgram.getListing();
        monitor = new ConsoleTaskMonitor();

        assetNames = loadSeedAssetNames(outRoot);
        assetAddresses = findAssetAddresses();
        fidMatches = new LinkedHashMap<>();
        setupFid();

        Map<String, List<String>> assetXrefs = new LinkedHashMap<>();
        for (String name : assetNames) {
            assetXrefs.put(name, new ArrayList<String>());
        }

        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        decomp.openProgram(currentProgram);

        List<String> functionJson = new ArrayList<>();
        FunctionIterator functions = currentProgram.getFunctionManager()
                .getFunctions(true);
        int count = 0;
        int okCount = 0;
        int failCount = 0;
        Set<String> usedNames = new LinkedHashSet<>();

        while (functions.hasNext()) {
            if (monitor.isCancelled()) {
                break;
            }
            Function func = functions.next();
            count++;
            Address entry = func.getEntryPoint();
            String fname = uniqueName(usedNames, filenameFor(func));
            usedNames.add(fname);

            List<String> dataRefs = new ArrayList<>();
            List<String> callees = new ArrayList<>();
            List<String> callers = new ArrayList<>();
            collectDataRefs(func, entry, dataRefs, assetXrefs);
            try {
                for (Function c : func.getCalledFunctions(monitor)) {
                    callees.add(jstr(addr(c.getEntryPoint())));
                }
            } catch (Exception ignored) {
            }
            try {
                for (Function c : func.getCallingFunctions(monitor)) {
                    callers.add(jstr(addr(c.getEntryPoint())));
                }
            } catch (Exception ignored) {
            }
            java.util.Collections.sort(callees);
            java.util.Collections.sort(callers);

            String ctext = null;
            String error = "";
            try {
                DecompileResults res = decomp.decompileFunction(
                        func, 30, monitor);
                if (res != null) {
                    error = res.getErrorMessage() == null
                            ? "" : res.getErrorMessage();
                    if (res.decompileCompleted()
                            && res.getDecompiledFunction() != null) {
                        ctext = res.getDecompiledFunction().getC();
                    }
                }
            } catch (Exception exc) {
                error = exc.toString();
            }

            File cfile = new File(outDir, fname);
            FileWriter fw = new FileWriter(cfile);
            try {
                if (ctext != null && ctext.length() > 0) {
                    okCount++;
                    fw.write("/* " + func.getName() + " @ " + addr(entry)
                            + " (" + func.getBody().getNumAddresses()
                            + " bytes) */\n");
                    fw.write(ctext);
                    if (!ctext.endsWith("\n")) {
                        fw.write("\n");
                    }
                }
                else {
                    failCount++;
                    fw.write("/* DECOMPILE FAILED @ " + addr(entry) + " */\n");
                    fw.write("/* " + error.replace("\n", " ") + " */\n");
                }
            }
            finally {
                fw.close();
            }

            StringBuilder fj = new StringBuilder();
            fj.append("{");
            fj.append("\"name\":").append(jstr(func.getName())).append(",");
            fj.append("\"entry\":").append(jstr(addr(entry))).append(",");
            fj.append("\"size\":").append(func.getBody().getNumAddresses())
              .append(",");
            fj.append("\"file\":").append(jstr(fname)).append(",");
            fj.append("\"decompiled\":").append(ctext != null).append(",");
            fj.append("\"error\":").append(jstr(error)).append(",");
            fj.append("\"callees\":").append(listJson(callees)).append(",");
            fj.append("\"callers\":").append(listJson(callers)).append(",");
            fj.append("\"is_fid_match\":").append(fidMatches.containsKey(addr(entry)))
              .append(",");
            fj.append("\"fid_name\":").append(jstr(fidMatches.get(addr(entry))))
              .append(",");
            fj.append("\"data_refs\":").append(listJson(dataRefs));
            fj.append("}");
            functionJson.add(fj.toString());

            if (count % 50 == 0) {
                println("[ghidra-export] " + binName + ": " + count
                        + " functions, " + okCount + " decompiled, "
                        + failCount + " failed");
            }
        }

        validatedAssetRefs = 0;
        for (List<String> xrefs : assetXrefs.values()) {
            validatedAssetRefs += xrefs.size();
        }

        StringBuilder assets = new StringBuilder();
        assets.append("{");
        boolean firstAsset = true;
        for (String name : assetNames) {
            if (!firstAsset) {
                assets.append(",");
            }
            firstAsset = false;
            List<String> addrs = new ArrayList<>();
            List<String> offsets = new ArrayList<>();
            for (Address a : assetAddresses.getOrDefault(
                    name, new ArrayList<Address>())) {
                addrs.add(jstr(addr(a)));
                Long fo = fileOffset(a);
                offsets.add(fo == null ? "null" : fo.toString());
            }
            assets.append(jstr(name)).append(":{");
            assets.append("\"addresses\":").append(listJson(addrs)).append(",");
            assets.append("\"file_offsets\":").append(listJson(offsets))
                  .append(",");
            assets.append("\"xrefs\":")
                  .append(listJson(assetXrefs.get(name)));
            assets.append("}");
        }
        assets.append("}");

        StringBuilder index = new StringBuilder();
        index.append("{\n");
        index.append("\"bin\":").append(jstr(binName)).append(",\n");
        index.append("\"program\":").append(jstr(binName)).append(",\n");
        index.append("\"language\":")
             .append(jstr(currentProgram.getLanguageID().toString()))
             .append(",\n");
        index.append("\"compiler_spec\":")
             .append(jstr(currentProgram.getCompilerSpec()
                     .getCompilerSpecID().toString())).append(",\n");
        index.append("\"image_base\":")
             .append(jstr(addr(currentProgram.getImageBase()))).append(",\n");
        index.append("\"function_count\":").append(count).append(",\n");
        index.append("\"decompiled_count\":").append(okCount).append(",\n");
        index.append("\"failed_count\":").append(failCount).append(",\n");
        index.append("\"fid_available\":").append(fidAvailable).append(",\n");
        index.append("\"fid_status\":").append(jstr(fidStatus)).append(",\n");
        index.append("\"fid_match_count\":").append(fidMatches.size())
             .append(",\n");
        index.append("\"asset_xref_stats\":{")
             .append("\"candidate_count\":").append(candidateAssetRefs)
             .append(",\"validated_count\":").append(validatedAssetRefs)
             .append(",\"discarded_count\":").append(
                     candidateAssetRefs - validatedAssetRefs)
             .append(",\"validated_nonempty_assets\":").append(
                     nonEmptyAssetXrefs(assetXrefs))
             .append("},\n");
        index.append("\"assets\":").append(assets).append(",\n");
        index.append("\"functions\":[");
        for (int i = 0; i < functionJson.size(); i++) {
            if (i > 0) {
                index.append(",");
            }
            index.append("\n").append(functionJson.get(i));
        }
        index.append("\n]\n}\n");

        FileWriter iw = new FileWriter(new File(outDir, "index.json"));
        try {
            iw.write(index.toString());
        }
        finally {
            iw.close();
        }
        decomp.dispose();
        println("[ghidra-export] wrote " + new File(outDir, "index.json")
                + " (" + count + " functions, " + okCount + " decompiled, "
                + failCount + " failed)");
    }

    private Map<String, List<Address>> findAssetAddresses() throws Exception {
        Map<String, List<Address>> found = new LinkedHashMap<>();
        for (String name : assetNames) {
            byte[] needle = name.getBytes("US-ASCII");
            List<Address> addrs = new ArrayList<>();
            Address start = memory.getMinAddress();
            while (start != null && !monitor.isCancelled()) {
                Address hit = memory.findBytes(start, needle, null, true,
                        monitor);
                if (hit == null) {
                    break;
                }
                addrs.add(hit);
                start = hit.add(1);
            }
            if (!addrs.isEmpty()) {
                found.put(name, addrs);
            }
        }
        return found;
    }

    private void collectDataRefs(Function func, Address entry,
            List<String> out, Map<String, List<String>> assetXrefs) {
        Set<String> seen = new LinkedHashSet<>();
        InstructionIterator it = listing.getInstructions(func.getBody(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            boolean flow = false;
            try {
                flow = ins.getFlowType().isCall() || ins.getFlowType().isJump();
            } catch (Exception ignored) {
            }

            // Direct memory references.
            try {
                for (ghidra.program.model.symbol.Reference r
                        : ins.getReferencesFrom()) {
                    if (!r.getReferenceType().isData()) {
                        continue;
                    }
                    Address to = r.getToAddress();
                    if (to == null || !to.getAddressSpace().isMemorySpace()) {
                        continue;
                    }
                    String text = printableStringAt(to);
                    String key = addr(to) + "|" + r.getReferenceType();
                    if (seen.add(key)) {
                        out.add("{\"address\":" + jstr(addr(to))
                                + ",\"type\":" + jstr(r.getReferenceType()
                                        .toString())
                                + ",\"text\":" + jstr(text)
                                + ",\"confidence\":\"direct\"}");
                    }
                    candidateAssetRefs++;
                    AssetHit hit = strictAssetHit(to);
                    if (hit != null) {
                        addXref(assetXrefs, hit.name, entry,
                                func.getName(), to, hit.text, "direct");
                    }
                }
            } catch (Exception ignored) {
            }

            if (flow) {
                continue;
            }
            for (int oi = 0; oi < ins.getNumOperands(); oi++) {
                Object[] objs = ins.getOpObjects(oi);
                for (Object obj : objs) {
                    if (obj instanceof Scalar) {
                        long v = ((Scalar) obj).getValue() & 0xffffL;
                        if (v >= 0x1000 && v < 0x2000) {
                            Address a = address("6000:"
                                    + String.format("%04x", v));
                            if (a != null) {
                                String text = printableStringAt(a);
                                if (text != null) {
                                    String key = "s|" + a + "|" + v;
                                    if (seen.add(key)) {
                                        out.add("{\"address\":"
                                                + jstr(addr(a))
                                                + ",\"type\":\"scalar-offset\""
                                                + ",\"value\":" + jstr(
                                                        "0x" + Long.toHexString(v))
                                                + ",\"text\":" + jstr(text)
                                                + ",\"confidence\":\"low\"}");
                                    }
                                }
                                candidateAssetRefs++;
                                AssetHit hit = strictAssetHit(a);
                                if (hit != null) {
                                    addXref(assetXrefs, hit.name, entry,
                                            func.getName(), a, hit.text,
                                            "scalar");
                                }
                            }
                        }
                    }
                    else if (obj instanceof Address) {
                        Address a = (Address) obj;
                        if (!a.getAddressSpace().isMemorySpace()) {
                            continue;
                        }
                        candidateAssetRefs++;
                        AssetHit hit = strictAssetHit(a);
                        if (hit != null) {
                            addXref(assetXrefs, hit.name, entry,
                                    func.getName(), a, hit.text, "operand");
                        }
                    }
                }
            }
        }
    }

    private static class AssetHit {
        String name;
        String text;
        AssetHit(String name, String text) {
            this.name = name;
            this.text = text;
        }
    }

    /**
     * Strict xref validation: read the bytes at the target address, require a
     * printable NUL-terminated 8.3-style name, and compare it byte-for-byte
     * with a name from re_out/seeds.json.  A leading "C:" drive prefix is
     * allowed because the DOS strings in Main.exe use it.
     */
    private AssetHit strictAssetHit(Address a) {
        if (a == null || !a.getAddressSpace().isMemorySpace()) {
            return null;
        }
        String text = asciiStringAt(a);
        if (text == null) {
            return null;
        }
        String candidate = text;
        if (candidate.length() > 2
                && (candidate.charAt(0) == 'C' || candidate.charAt(0) == 'c')
                && candidate.charAt(1) == ':') {
            candidate = candidate.substring(2);
        }
        for (String name : assetNames) {
            if (name.equalsIgnoreCase(candidate)) {
                return new AssetHit(name, text);
            }
        }
        return null;
    }

    private String asciiStringAt(Address a) {
        if (a == null) {
            return null;
        }
        List<Byte> raw = new ArrayList<>();
        try {
            for (int i = 0; i < 64; i++) {
                byte b = memory.getByte(a.add(i));
                if (b == 0) {
                    break;
                }
                int v = b & 0xff;
                if (v < 0x20 || v > 0x7e) {
                    return null;
                }
                raw.add(b);
            }
        }
        catch (MemoryAccessException exc) {
            return null;
        }
        if (raw.size() < 3) {
            return null;
        }
        byte[] bytes = new byte[raw.size()];
        for (int i = 0; i < raw.size(); i++) {
            bytes[i] = raw.get(i);
        }
        return new String(bytes, StandardCharsets.US_ASCII);
    }

    private void addXref(Map<String, List<String>> assetXrefs, String asset,
            Address entry, String funcName, Address stringAddr,
            String matchedText, String kind) {
        List<String> refs = assetXrefs.get(asset);
        if (refs == null) {
            return;
        }
        Address seedAddr = null;
        List<Address> seeds = assetAddresses.get(asset);
        if (seeds != null && !seeds.isEmpty()) {
            seedAddr = seeds.get(0);
        }
        String item = "{\"function\":" + jstr(addr(entry))
                + ",\"function_name\":" + jstr(funcName)
                + ",\"asset_address\":" + jstr(addr(seedAddr))
                + ",\"string_address\":" + jstr(addr(stringAddr))
                + ",\"matched_text\":" + jstr(matchedText)
                + ",\"kind\":" + jstr(kind) + "}";
        if (!refs.contains(item)) {
            refs.add(item);
        }
    }

    private int nonEmptyAssetXrefs(Map<String, List<String>> assetXrefs) {
        int n = 0;
        for (List<String> x : assetXrefs.values()) {
            if (!x.isEmpty()) {
                n++;
            }
        }
        return n;
    }

    private Set<String> loadSeedAssetNames(String outRoot) {
        Set<String> names = new LinkedHashSet<>();
        File seeds = new File(outRoot, "seeds.json");
        try {
            String text = new String(Files.readAllBytes(seeds.toPath()),
                    StandardCharsets.UTF_8);
            Matcher m = Pattern.compile(
                    "\"([A-Za-z0-9_.-]+\\.(?:GP|PLD))\"\\s*:\\s*\\[")
                    .matcher(text);
            while (m.find()) {
                names.add(m.group(1).toUpperCase());
            }
        }
        catch (Exception exc) {
            println("[ghidra-export] cannot read " + seeds + ": " + exc);
        }
        if (names.isEmpty()) {
            for (String name : ASSET_NAMES) {
                names.add(name);
            }
        }
        println("[ghidra-export] seed asset names: " + names.size());
        return names;
    }

    private void setupFid() {
        try {
            FidFileManager manager = FidFileManager.getInstance();
            manager.load();
            FidService service = new FidService();
            if (!service.canProcess(currentProgram.getLanguage())) {
                fidAvailable = false;
                fidStatus = "FID libraries cannot process language "
                        + currentProgram.getLanguageID()
                        + " (installed Visual Studio FID databases are 32/64-bit only)";
                println("[ghidra-export] " + fidStatus);
                return;
            }
            FidQueryService query = service.openFidQueryService(
                    currentProgram.getLanguage(), false);
            try {
                List<FidSearchResult> results = service.processProgram(
                        currentProgram, query,
                        service.getDefaultScoreThreshold(), monitor);
                for (FidSearchResult result : results) {
                    if (result.function == null || result.matches == null
                            || result.matches.isEmpty()) {
                        continue;
                    }
                    FidMatch best = null;
                    for (FidMatch match : result.matches) {
                        if (best == null || match.getOverallScore()
                                > best.getOverallScore()) {
                            best = match;
                        }
                    }
                    if (best != null) {
                        fidMatches.put(addr(result.function.getEntryPoint()),
                                best.getFunctionRecord().getName());
                    }
                }
                fidAvailable = true;
                fidStatus = "FID match complete with " + fidMatches.size()
                        + " matched functions";
                println("[ghidra-export] " + fidStatus);
            }
            finally {
                query.close();
            }
        }
        catch (Exception exc) {
            fidAvailable = false;
            fidStatus = "FID setup failed: " + exc;
            println("[ghidra-export] " + fidStatus);
        }
    }

    private String printableStringAt(Address a) {
        if (a == null) {
            return null;
        }
        List<Byte> raw = new ArrayList<>();
        try {
            for (int i = 0; i < 96; i++) {
                byte b = memory.getByte(a.add(i));
                if (b == 0) {
                    break;
                }
                raw.add(b);
            }
        }
        catch (MemoryAccessException exc) {
            return null;
        }
        if (raw.size() < 2) {
            return null;
        }
        int printable = 0;
        for (byte b : raw) {
            int v = b & 0xff;
            if (v >= 0x20 && v <= 0x7e) {
                printable++;
            }
        }
        if (printable < Math.max(2, (raw.size() * 3) / 4)) {
            return null;
        }
        byte[] bytes = new byte[raw.size()];
        for (int i = 0; i < raw.size(); i++) {
            bytes[i] = raw.get(i);
        }
        return new String(bytes, sjis);
    }

    private Long fileOffset(Address a) {
        try {
            var info = memory.getAddressSourceInfo(a);
            if (info != null) {
                return info.getFileOffset();
            }
        }
        catch (Exception ignored) {
        }
        return null;
    }

    private Address address(String s) {
        try {
            return currentProgram.getAddressFactory().getAddress(s);
        }
        catch (Exception exc) {
            return null;
        }
    }

    private String filenameFor(Function func) {
        String entry = addr(func.getEntryPoint());
        String seg = entry;
        String off = "";
        int colon = entry.indexOf(':');
        if (colon >= 0) {
            seg = entry.substring(0, colon);
            off = entry.substring(colon + 1);
        }
        return sanitize(seg) + "_" + sanitize(off) + "_"
                + sanitize(func.getName()) + ".c";
    }

    private String uniqueName(Set<String> used, String name) {
        if (!used.contains(name)) {
            return name;
        }
        int dot = name.lastIndexOf('.');
        String root = dot >= 0 ? name.substring(0, dot) : name;
        String ext = dot >= 0 ? name.substring(dot) : "";
        int n = 2;
        while (used.contains(root + "_" + n + ext)) {
            n++;
        }
        return root + "_" + n + ext;
    }

    private String sanitize(String s) {
        return s.replaceAll("[^A-Za-z0-9_.-]+", "_");
    }

    private String addr(Address a) {
        return a == null ? null : a.toString();
    }

    private String listJson(List<String> items) {
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < items.size(); i++) {
            if (i > 0) {
                sb.append(",");
            }
            sb.append(items.get(i));
        }
        sb.append("]");
        return sb.toString();
    }

    private String jstr(String s) {
        if (s == null) {
            return "null";
        }
        StringBuilder sb = new StringBuilder("\"");
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '"': sb.append("\\\""); break;
                case '\\': sb.append("\\\\"); break;
                case '\n': sb.append("\\n"); break;
                case '\r': sb.append("\\r"); break;
                case '\t': sb.append("\\t"); break;
                default:
                    if (c < 0x20 || c > 0x7e) {
                        sb.append(String.format("\\u%04x", (int) c));
                    }
                    else {
                        sb.append(c);
                    }
            }
        }
        sb.append("\"");
        return sb.toString();
    }
}
