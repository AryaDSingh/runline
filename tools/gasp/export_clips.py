"""Pass 1 of scripts/import-gasp.ps1, run inside the GASP project (UnrealEditor-Cmd -run=pythonscript).

Retargets each clip in the list from the UEFN mannequin to the UE5 Manny with GASP's own retargeter,
exports the results as FBX, and deletes the temporary retargeted assets again.
Environment: RUNLINE_GASP_LIST (the clip list), RUNLINE_GASP_OUT (the FBX folder).
"""
import os
import unreal

LIST = os.environ["RUNLINE_GASP_LIST"]
OUT = os.environ["RUNLINE_GASP_OUT"]
TEMP = "/Game/_RunlineExport"
SOURCE_MESH = "/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin"
TARGET_MESH = "/Game/Characters/UE5_Mannequins/Meshes/SKM_Manny_Simple"
RETARGETER = "/Game/Characters/UE5_Mannequins/Rigs/RTG_UEFN_to_UE5_Mannequin"


def clips():
    for line in open(LIST, encoding="utf8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split(",")
        yield fields[0], fields[1]


def main():
    os.makedirs(OUT, exist_ok=True)
    lib = unreal.EditorAssetLibrary
    source = unreal.load_asset(SOURCE_MESH)
    target = unreal.load_asset(TARGET_MESH)
    rtg = unreal.load_asset(RETARGETER)
    if not (source and target and rtg):
        raise RuntimeError("Runline GASP: missing mesh or retargeter in this project")
    for slot, path in clips():
        data = unreal.EditorAssetLibrary.find_asset_data(path)
        if not data.is_valid():
            raise RuntimeError("Runline GASP: no asset " + path)
        inputs = unreal.IKRetargetBatchOperationInputs()
        inputs.assets_to_retarget = [data]
        inputs.source_mesh = source
        inputs.target_mesh = target
        inputs.ik_retarget_asset = rtg
        inputs.target_path = TEMP
        inputs.prefix = "R_"
        inputs.include_referenced_assets = False
        inputs.overwrite_existing_files = True
        made = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
        seqs = [a.get_asset() for a in made if isinstance(a.get_asset(), unreal.AnimSequence)]
        if len(seqs) != 1:
            raise RuntimeError("Runline GASP: retargeting %s made %d sequences" % (path, len(seqs)))
        task = unreal.AssetExportTask()
        task.object = seqs[0]
        task.filename = os.path.join(OUT, slot + ".fbx")
        task.automated = True
        task.prompt = False
        task.replace_identical = True
        task.exporter = unreal.AnimSequenceExporterFBX()
        if not unreal.Exporter.run_asset_export_task(task):
            raise RuntimeError("Runline GASP: FBX export failed for " + slot)
        unreal.log("Runline GASP: exported %s -> %s" % (path, task.filename))
    lib.delete_directory(TEMP)


main()
