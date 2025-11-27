%:
	${CC} -undef -x assembler-with-cpp $(shell pwd)/gpu/$@.dtso -I ${KERNEL_INCLUDE} -E -o $(shell pwd)/gpu/$@.dtso.preprocessed
	${DTC} -O dtb -o $(shell pwd)/gpu/$@.dtbo $(shell pwd)/gpu/$@.dtso.preprocessed

clean:
	rm -rf *.preprocessed