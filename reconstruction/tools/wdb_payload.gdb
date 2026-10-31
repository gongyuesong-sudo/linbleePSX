set pagination off
set confirm off
break *0x4010c2
continue
printf "AT_CALL view=%x\n", $eax
delete 1
break *0x401117
continue
printf "MSGBOX_HIT errcode=%d\n", *(int*)0x401124
dump binary memory /tmp/payload_view.bin 0x12e0000 0x1330000
printf "DUMP_DONE\n"
kill
quit
