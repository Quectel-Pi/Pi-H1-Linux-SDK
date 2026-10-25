SUMMARY = "Fast tokenizers for python (Rust, prebuilt cp39-abi3 aarch64 wheel)"
DESCRIPTION = "HuggingFace tokenizers 0.22.2 as imported by the Qwen3.5 runtime \
(qwen_server.py:40, request_frontend.py:14, image_frontend.py:28): \
'from tokenizers import Tokenizer'.  It is a Rust extension shipped as an \
abi3 wheel (cp39-abi3), which is why the cp313 interpreter on the board can \
import it although the wheel was not built for 3.13. \
\
The package name on the board is python3-tokenizers (what an OE recipe would be \
called, and what ../qwen35-npu/qwen35-npu_1.0.bb RDEPENDS on), so the recipe \
provides that name.  Nothing else from the huggingface stack is pulled in: the \
wheel's optional huggingface_hub dependency is not imported by tokenizers/ \
itself (checked in tokenizers/__init__.py, which imports only enum, typing, \
.tokenizers and .implementations)."
HOMEPAGE = "https://github.com/huggingface/tokenizers"
LICENSE = "Apache-2.0"
# The wheel carries no LICENSE file (only METADATA), so the licence text is the
# canonical one from poky; do_install copies it into the package's docdir.
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10"

require qwen35-wheel.inc

# 3290736 bytes; the sha256 is PyPI's, the file was fetched and re-hashed locally.
QWEN35_WHEEL_URL = "https://files.pythonhosted.org/packages/d6/84/7990e799f1309a8b87af6b948f31edaa12a3ed22d11b352eaf4f4b2e5753"
QWEN35_WHEEL_NAME = "tokenizers-${PV}-cp39-abi3-manylinux_2_17_aarch64.manylinux2014_aarch64.whl"
SRC_URI[wheel.sha256sum] = "2249487018adec45d6e3554c71d46eb39fa8ea67156c640f7513eb26f318cec7"

# qwen35-npu RDEPENDS on python3-tokenizers (DEPENDS_NOTES.md §4): keep the OE
# package name resolvable both at runtime and for future DEPENDS users.
PROVIDES += "python3-tokenizers"
RPROVIDES:${PN} += "python3-tokenizers"

# libstdc++/libgcc_s come from the Debian rootfs; only the interpreter is an OE
# package here.  (The OE file-rdeps scan is skipped in qwen35-wheel.inc.)
RDEPENDS:${PN} = "python3-core"

do_install() {
	qwen35_install_wheel_tree ${D}${PYTHON_DIST_PACKAGES}

	# The abi3 tag is what makes this wheel usable on python 3.13 -- if a bump
	# ever ships a version-specific .so, fail here instead of on the board.
	if [ ! -f ${D}${PYTHON_DIST_PACKAGES}/${PN}/tokenizers.abi3.so ]; then
		bbfatal "${PN}: no tokenizers.abi3.so in ${QWEN35_WHEEL_NAME}; the wheel is not abi3 and will not import on python 3.13"
	fi

	# Attribution: the wheel has no licence file of its own.
	install -d ${D}${docdir}/${PN}
	install -m 0644 ${COMMON_LICENSE_DIR}/Apache-2.0 ${D}${docdir}/${PN}/LICENSE
}

FILES:${PN} = "${PYTHON_DIST_PACKAGES}/${PN} \
               ${PYTHON_DIST_PACKAGES}/${PN}-${PV}.dist-info \
               ${docdir}/${PN} \
"
