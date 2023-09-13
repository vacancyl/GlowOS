if [ ! -d "../lib" || ! -d "../build" ];then
   echo "dependent dir don\`t exist!"
   cwd=$(pwd)
   cwd=${cwd##*/}
   cwd=${cwd%/}
   if [ $cwd != "command" ];then
      echo -e "you\`d better in command dir\n"
      exit
   fi 
   
fi

BIN="prog_no_arg"
CFLAGS="-Wall -c -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes -Wsystem-headers"

### LIB= "../lib"
OBJS="../build/string.o ../build/syscall.o ../build/stdio.o ../build/debug.o ../build/print.o"

DD_IN=$BIN
DD_OUT="/home/vacancy/bochs/bin/hd60M.img" 

### compile ###
gcc -m32 $CFLAGS -I "../lib" -o $BIN".o" $BIN".c"
ld -m elf_i386 -e main $BIN".o" $OBJS -o $BIN
SEC_CNT=$(ls -l $BIN|awk '{printf("%d", ($5+511)/512)}')

if [ -f $BIN ];then
   dd if=./$DD_IN of=$DD_OUT bs=512 \
   count=$SEC_CNT seek=300 conv=notrunc
fi

