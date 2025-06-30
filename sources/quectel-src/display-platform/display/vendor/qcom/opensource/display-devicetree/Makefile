%:
	${CC} -undef -x assembler-with-cpp $(shell pwd)/display/$@.dtso -I ${KERNEL_INCLUDE} -E -o $(shell pwd)/display/$@.dtso.preprocessed
	${DTC} -O dtb -o $(shell pwd)/display/$@.dtbo $(shell pwd)/display/$@.dtso.preprocessed

clean:
	rm -rf *.preprocessed
