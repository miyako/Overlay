//%attributes = {"invisible":true}
#DECLARE($state : Object; $result : Object)
Use ($state)
	$state.count+=1
	$state.results.push(JSON Stringify($result))
End use
