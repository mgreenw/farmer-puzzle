echo "Running tests...this may take a while."
for i in `seq 1 100`;
do
  echo "Test $i"
  code=$((1 + RANDOM % 999999))
  code_str="$code"
  ./farmer_puzzle <<< $code_str | grep $code_str &> /dev/null
  if [ $? == 0 ]; then
   echo "Test success!"
  else
    echo "Test failed. Expected output $code_str"
  fi
done

# for code in test_codes/*.txt; do
#   ./farmer_puzzle < "$code" | grep ''
# done
