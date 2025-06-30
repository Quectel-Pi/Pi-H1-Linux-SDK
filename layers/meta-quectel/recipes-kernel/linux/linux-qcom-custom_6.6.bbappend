do_unpack_overlay() {
	cp -r ${WORKSPACE}/src/qcom-6.6-overlay/* ${S}	
}

addtask unpack_overlay after do_unpack before do_patch

