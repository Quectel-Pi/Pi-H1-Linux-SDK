# Pin parser_0.1's SRCREV (meta-qcom-hwe/recipes-edgegateway/parser).
#
# Same reason as tensorflow-lite_%.bbappend next to this directory: the recipe
# says SRCREV = "${AUTOREV}", and AUTOREV is resolved during *parsing* because
# base.bbclass expands fetcher_hashes_dummyfunc[vardepvalue] via
# bb.fetch.get_hashvalue(d) to compute the recipe's signature.  That makes every
# bitbake invocation depend on `git ls-remote git.codelinaro.org`, and any
# network hiccup aborts the whole parse with ExpansionError instead of failing
# one recipe's do_fetch.
#
# The SHA below is the head of branch edgegateway.lnx.1.0.  Nothing else changes:
# a 40-character SRCREV is a plain string, so no network access happens while
# parsing, and PV has no SRCPV in it, so pinning does not alter the version.
#
# To move it: git ls-remote https://git.codelinaro.org/clo/le/platform/vendor/qcom-opensource/edgegateway.git refs/heads/edgegateway.lnx.1.0
SRCREV = "75415c84792bcf779f9cc6e6e90ae8a4169f98aa"
