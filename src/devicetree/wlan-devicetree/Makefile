%:
	echo "Processing target $@"
	${CC} -undef -x assembler-with-cpp $@.dtso -I ${KERNEL_INCLUDE} -E -o $@.dts.preprocessed
	${DTC} -O dtb -o $@.dtbo $@.dts.preprocessed

clean:
	rm -rf *.dtbo
	rm -rf *.preprocessed
