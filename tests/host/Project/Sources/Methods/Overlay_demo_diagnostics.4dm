//%attributes = {"invisible":true}
#DECLARE($modal : Object)->$report : Object
var $result; $diagnostic : Object
var $stderr; $previous : Text
$report:=New object
$previous:=Method called on error(ek local)
$stderr:="{\"protocol\":1,\"error\":{\"code\":\"image_complexity\",\"message\":\"clickable PNG exceeds 512 alpha-coverage rectangles\"}}\n"
$result:=New object("ok"; False; "stderr"; $stderr; "exitCode"; 2; "error"; New object("code"; "cli_error"; "message"; $stderr))
$report.complexity:=Overlay_error_details($result)
$report.malformed:=Overlay_error_details(New object("ok"; False; "stderr"; "not JSON"; "exitCode"; 2; "error"; New object("code"; "cli_error"; "message"; "helper failed")))
$report.invalidPayloads:=New collection
For each ($stderr; New collection("true"; "false"; "null"; "1"; "[]"; "{\"protocol\":true,\"error\":{\"code\":\"x\",\"message\":\"y\"}}"; "{\"protocol\":1,\"error\":false}"; "{\"protocol\":1,\"error\":{\"code\":1,\"message\":true}}"; "{\"protocol\":2,\"error\":{\"code\":\"x\",\"message\":\"y\"}}"))
	$report.invalidPayloads.push(Overlay_error_details(New object("ok"; False; "stderr"; $stderr; "error"; New object("code"; "cli_error"; "message"; "helper failed"))))
End for each
$stderr:="{\"protocol\":1,\"error\":{\"code\":\"image_complexity\",\"message\":\"too complex\"}}"
$report.invalidPayloads.push(Overlay_error_details(New object("ok"; False; "stderr"; $stderr+Char(10)+"trailing diagnostic"; "error"; New object("code"; "cli_error"; "message"; "helper failed"))))
$report.invalidPayloads.push(Overlay_error_details(New object("ok"; False; "stderr"; $stderr+Char(10)+$stderr+Char(10); "error"; New object("code"; "cli_error"; "message"; "helper failed"))))
$report.cancelled:=Overlay_error_details(New object("ok"; False; "cancelled"; True; "error"; New object("code"; "cancelled"; "message"; "Overlay was cancelled")))
$report.notCancelled:=New collection
$report.notCancelled.push(Overlay_error_details(New object("ok"; False; "error"; New object("code"; "cancelled"; "message"; "Not an acknowledged cancellation"))))
$report.notCancelled.push(Overlay_error_details(New object("ok"; False; "cancelled"; False; "error"; New object("code"; "cancelled"; "message"; "Cancellation lost a race"))))
$report.notCancelled.push(Overlay_error_details(New object("ok"; False; "cancelled"; True; "error"; New object("code"; "cli_error"; "message"; "Not a cancellation"))))
$report.notCancelled.push(Overlay_error_details(New object("ok"; True; "cancelled"; True; "error"; New object("code"; "cancelled"; "message"; "Contradictory success"))))
$report.prelaunch:=Overlay_error_details(New object("ok"; False; "error"; New object("code"; "missing_executable"; "message"; "Bundled CLI is missing")))
$report.invalidWrapper:=Overlay_error_details(New object("ok"; False; "stderr"; "raw diagnostic"; "error"; False))
$report.nullResult:=Overlay_error_details(Null)
$report.modal:=Overlay_error_details($modal)
$report.handlerRestored:=(Method called on error(ek local)=$previous)
