#!/bin/bash
###########
#Test homework for shell parallel excution.
#author: michael
#date:	 2014-04-15
###########
function caculate()
{
	result=0
	for i in `seq $start $end`
	do
		result=$(($result+$i))
	done
	echo "$result" >&5
}

[ "${#@}" -lt 1 ] && { echo  "usage: $0 sum"; exit -1; }
threads=${2:-"10"}

pipefile="${$}.pipo"
mkfifo $pipefile
exec 3<>$pipefile
rm -rf $pipefile
mkfifo $pipefile
exec 5<>$pipefile
rm -rf $pipefile
for i in `seq 1 $threads`
do
	echo "$i" >&3 
done

over="false"
end="0"
step=${3:-"100"}
index=$((($1-1)/($step+1)+1)) #run times of the function. in principle
while [ "$over" = "false" ]
do
	read -u3
	start=$(($end+1))
	end=$(($start+step))
	if [ "$end" -gt "$1" ] 
	then
		end=$1
		over="true"
	fi
	{
		caculate $start $end
		echo "" >&3
	} &
	
done
wait
result=0
while [ "$index" -gt 0 ] && read -u5 value 
do
	result=$(($result+$value))
	index=$(($index-1))
done
echo "result is $result"
