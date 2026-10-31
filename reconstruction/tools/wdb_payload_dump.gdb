set pagination off
set confirm off
break *0x4010c2
continue
printf "AT_CALL view=%x\n", $eax
delete 1
break *0x4010c4
continue
printf "POST_CALL eax=%x\n", $eax
dump binary memory /tmp/mem_rwx_a.bin 0x1060000 0x1090000
dump binary memory /tmp/mem_rwx_b.bin 0x1160000 0x12e0000
dump binary memory /tmp/mem_view.bin 0x12e0000 0x1336000
printf "DUMPS_DONE\n"
kill
quit
