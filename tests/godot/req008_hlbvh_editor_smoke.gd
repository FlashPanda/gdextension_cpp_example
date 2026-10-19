@tool
extends SceneTree


func _init() -> void:
    call_deferred("_run")


func _find_plugin(node: Node) -> Node:
    var script = node.get_script()
    if script != null and script.resource_path.ends_with("raytrace_exporter_plugin.gd"):
        return node
    for child in node.get_children():
        var found := _find_plugin(child)
        if found != null:
            return found
    return null


func _run() -> void:
    await process_frame
    await process_frame
    var plugin := _find_plugin(root)
    if plugin == null:
        push_error("REQ008_EDITOR FAIL: exporter plugin was not loaded")
        quit(1)
        return
    var option: OptionButton = plugin.get("_acceleration_option")
    if option == null or option.item_count != 5 or option.get_item_id(4) != 4 or option.get_item_text(4) != "HLBVH":
        push_error("REQ008_EDITOR FAIL: HLBVH option missing or old IDs changed")
        quit(1)
        return
    option.select(4)
    if plugin.call("_current_acceleration_name") != "hlbvh":
        push_error("REQ008_EDITOR FAIL: HLBVH selection mapping")
        quit(1)
        return
    plugin.call("_update_button_progress", 0.5)
    if not option.disabled:
        push_error("REQ008_EDITOR FAIL: selection not locked during render")
        quit(1)
        return
    plugin.call("_reset_job_state")
    if option.disabled:
        push_error("REQ008_EDITOR FAIL: selection not unlocked after render")
        quit(1)
        return
    print("REQ008_EDITOR PASS option=hlbvh id=4 lock=ok")
    quit(0)
