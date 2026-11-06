#DECLARE -> $handle : cs.Overlay.OverlayHandle
// Run in a desktop host, not tool4d. Assign the returned handle to Form.overlayHandle.
var $png : 4D.File
var $options : Object
var $target : Text
$target:="macOS-universal"
If (Is Windows)
	$target:="Windows-x64"
End if
$png:=File(Folder(fk desktop folder).file("overlay.png").platformPath; fk platform path)
$options:=New object("image"; $png; "x"; 100; "y"; 100; "mode"; "clickable"; "timeoutMs"; 5000; "fadeInMs"; 150; "fadeOutMs"; 150; "opacity"; 0.9; "scale"; 1; "modal"; False)
$handle:=cs.Overlay.Overlay.new($target).show($options; Formula(CALL WORKER(1; Formula(Overlay_demo_done($1)); $1)))
// This method returns immediately. Store $handle in Form.overlayHandle in a form.
