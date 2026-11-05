property state : Object
property callback : Object
property worker : 4D.SystemWorker
property stdout : Text
property stderr : Text
property completed : Boolean
property dataType : Text
property encoding : Text
property hideWindow : Boolean
property currentDirectory : 4D.Folder
property inputErrors : Collection

Class constructor($state : Object; $callback : Object; $options : Object)
	var $duration; $fadeIn; $fadeOut : Real
	var $api : cs.Overlay
	This.state:=$state
	This.callback:=$callback
	This.stdout:=""
	This.stderr:=""
	This.completed:=False
	This.dataType:="text"
	This.encoding:="UTF-8"
	This.hideWindow:=True
	$duration:=0
	$fadeIn:=0
	$fadeOut:=0
	$api:=cs.Overlay.new()
	If ($options#Null)
		If ($api._number($options.timeoutMs))
			$duration:=$options.timeoutMs
		End if
		If ($api._number($options.fadeInMs))
			$fadeIn:=$options.fadeInMs
		End if
		If ($api._number($options.fadeOutMs))
			$fadeOut:=$options.fadeOutMs
		End if
	End if
	If ($duration>0)
		This.timeout:=($duration+$fadeIn+$fadeOut)/1000+10
		If (This.timeout<30)
			This.timeout:=30
		End if
		If (This.timeout>3730)
			This.timeout:=3730
		End if
	End if

Function start($prepared : Object; $stdin : Boolean; $png : Blob)
	var $previous : Text
	var $errors : Collection
	var CLI_LastErrors : Collection
	If (This.state.cancelled)
		This.finish(New object("ok"; False; "cancelled"; True; "error"; New object("code"; "cancelled"; "message"; "Cancelled before launch")))
		return
	End if
	If (Not($prepared.ok))
		This.finish($prepared)
		return
	End if
	This.currentDirectory:=$prepared.currentDirectory
	$previous:=Method called on error(ek local)
	CLI_LastErrors:=Null
	ON ERR CALL("_CaptureError"; ek local)
	This.worker:=4D.SystemWorker.new($prepared.command; This)
	ON ERR CALL($previous; ek local)
	$errors:=CLI_LastErrors
	CLI_LastErrors:=Null
	If (This.worker=Null)
		This.finish(New object("ok"; False; "error"; New object("code"; "launch_failed"; "message"; "4D could not create the system worker"; "details"; $errors)))
		return
	End if
	$previous:=Method called on error(ek local)
	CLI_LastErrors:=Null
	ON ERR CALL("_CaptureError"; ek local)
	If ($stdin)
		This.worker.postMessage($png)
	End if
	This.worker.closeInput()
	ON ERR CALL($previous; ek local)
	This.inputErrors:=CLI_LastErrors
	CLI_LastErrors:=Null
	If (This.state.cancelled | (This.inputErrors#Null))
		This.worker.terminate()
	End if

Function onData($worker : Object; $event : Object)
	This.stdout+=$event.data

Function onDataError($worker : Object; $event : Object)
	This.stderr+=$event.data

Function onTerminate($worker : Object; $event : Object)
	var $result : Object
	If (This.completed)
		return
	End if
	$result:=New object("ok"; False; "stdout"; This.stdout; "stderr"; This.stderr)
	If ($worker.exitCode#Null)
		$result.exitCode:=$worker.exitCode
	End if
	If ($worker.errors#Null)
		$result.errors:=$worker.errors
	End if
	Case of
		: (This.state.cancelled)
			$result.cancelled:=True
			$result.error:=New object("code"; "cancelled"; "message"; "Overlay was cancelled")
		: (This.inputErrors#Null)
			$result.error:=New object("code"; "process_error"; "message"; "Could not write or close stdin"; "details"; This.inputErrors)
		: ($worker.errors#Null)
			$result.error:=New object("code"; "process_error"; "message"; "System worker execution failed"; "details"; $worker.errors)
		: ($worker.exitCode=Null)
			$result.error:=New object("code"; "process_terminated"; "message"; "Process did not exit normally (including timeout)")
		: ($worker.exitCode#0)
			$result.error:=New object("code"; "cli_error"; "message"; This.stderr)
		Else
			$result.ok:=True
			$result:=cs.Overlay.new()._decode($result; "show")
	End case
	This.finish($result)

Function finish($result : Object)
	var $callback : Object
	If (This.completed)
		return
	End if
	This.completed:=True
	Use (This.state)
		This.state.resultJSON:=JSON Stringify($result)
		This.state.worker:=0
		This.state.done:=True
	End use
	$callback:=This.callback
	This.callback:=Null
	This.worker:=Null
	KILL WORKER
	If ($callback#Null)
		_Overlay_callback($callback; $result)
	End if
