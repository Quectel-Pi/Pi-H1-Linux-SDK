# Fix SRCREV_qcombluetoothext, which upstream assigns the *branch name* instead
# of the revision:
#
#   QCOM_BLUETOOTH_EXT_SRCREV   ?= "eaec2f1535cc20362db170631d79b698f3a13fbc"
#   QCOM_BLUETOOTH_EXT_SRCBRANCH ?= "..."                      (or similar)
#   SRCREV_qcombluetoothext     = "${QCOM_BLUETOOTH_EXT_SRCBRANCH}"   <-- bug
#
# SRCREV has to be a revision.  Given a branch name, bitbake resolves it by
# running `git ls-remote` against bluetooth_ext.git *while parsing*, because
# base.bbclass computes the recipe signature from
# fetcher_hashes_dummyfunc[vardepvalue] = "${@bb.fetch.get_hashvalue(d)}".
# The consequence is not a slow build, it is a dead one: with the remote
# unreachable the FetchError/NetworkAccess re-raised through ${@...} becomes
#
#   ERROR: ExpansionError during parsing .../fluoride_git.bb
#   bb.data_smart.ExpansionError: Failure expanding variable
#       fetcher_hashes_dummyfunc[vardepvalue] ... NetworkAccess
#   ERROR: Parsing halted due to errors
#
# and the whole parse aborts with zero recipes, so nothing at all can be built --
# including `bitbake -p` and `bitbake -e`.  Every other recipe in the tree is
# pinned, so this one file was the last thing standing between a fresh checkout
# and a parse that works offline.
#
# This is a typo fix, not a version change: it uses the recipe's own
# QCOM_BLUETOOTH_EXT_SRCREV variable, so the commit built is exactly the one the
# recipe already documents.  Drop this file if upstream corrects line 22.
SRCREV_qcombluetoothext = "${QCOM_BLUETOOTH_EXT_SRCREV}"
