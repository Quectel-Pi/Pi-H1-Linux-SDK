# Pin the SRCREV of the CodeLinaro tensorflow-lite recipes.
#
# Why this file exists
# -------------------
# The five tensorflow-lite recipes in meta-qcom-qim-product-sdk set
#
#     SRCREV = "${AUTOREV}"
#
# AUTOREV is resolved while *parsing*, not while building: base.bbclass wires
#     do_fetch[prefuncs] += "fetcher_hashes_dummyfunc"
#     fetcher_hashes_dummyfunc[vardepvalue] = "${@bb.fetch.get_hashvalue(d)}"
# so the parser runs `git ls-remote` against the remote to learn the branch head.
# That single network round trip is on the critical path of every `bitbake`
# invocation, including `bitbake -e` and the parse-only first stage of a build:
# a DNS blip, a TLS reset or an offline laptop turns into
#
#     ERROR: ExpansionError during parsing ... tensorflow-lite_2.16.1.qcom.bb
#     bb.data_smart.ExpansionError: Failure expanding variable
#         fetcher_hashes_dummyfunc[vardepvalue] ... FetchError
#
# and the *entire* parse aborts with zero recipes, so nothing can be built at all.
#
# What it pins
# ------------
# One line per PV.  The SHAs were resolved once, tarballs for all five are in
# DL_DIR (downloads/git2/git.codelinaro.org.clo.le.external.github.com.tensorflow.tensorflow.git),
# so `bitbake` needs no network from then on -- not even for a clean build dir.
#
# Pinning does NOT change PV: these recipes have no SRCPV in PV, so the version
# stays 2.16.1.qcom and no rebuild is triggered by this file.
#
# Why a bbappend and why the URL check below
# ------------------------------------------
# `SRCREV:pn-tensorflow-lite` looks tempting but is wrong: it is keyed on the
# recipe *name*, so it also hits layers/meta-tensorflow's own
# tensorflow-lite_2.16.1.bb (PV "2.16.1", no ".qcom"), whose SRC_URI points at
# github.com/tensorflow and which already carries a hard pin in tensorflow.inc
# (SRCREV = "5bc9d266...").  Overriding that one with a CodeLinaro SHA -- or
# with the empty string -- breaks a recipe that was fine, and with the empty
# string the parse dies with "Please set a valid SRCREV".  There is no
# per-layer override syntax, so the scoping has to be done on SRC_URI.
#
# An anonymous python function is used instead of a plain `SRCREV = "..."`,
# because the value has to depend on PV: a single assignment would either clobber
# the other four versions or be a no-op for four of the five.
#
# Keeping it current
# ------------------
# A new tensorflow-lite version upstream => add its line here.  Until then the
# recipe keeps its own SRCREV (AUTOREV => needs network at parse time), and the
# warning below names the missing PV rather than failing the build for everyone.

TFLITE_SRCREV_BY_PV = "\
    2.12.1.qcom:657f510f0c73a7b074eb9d0d9185a93331d2ce7e \
    2.13.1.qcom:bf49ba3c39b3251c04c4190961dfe50c27806338 \
    2.14.1.qcom:3933bd374868cb6b4cc63084aacc1d7f3a4fdc71 \
    2.15.0.qcom:a2917ae36c856aa62e2a110685caa0fdb9a3c60e \
    2.16.1.qcom:48bbbdbdee34b7f7fa0c786280ad41450a02a09f \
    "

python __anonymous() {
    codelinaro = "git.codelinaro.org"
    uri = d.getVar("SRC_URI") or ""
    if codelinaro not in uri:
        # meta-tensorflow's tensorflow-lite_2.16.1.bb (github.com/tensorflow,
        # branch r2.16): already pinned in tensorflow.inc, leave it alone.
        return

    table = {}
    for row in (d.getVar("TFLITE_SRCREV_BY_PV") or "").split():
        try:
            pv, sha = row.split(":")
        except ValueError:
            bb.fatal("tensorflow-lite_%.bbappend: malformed TFLITE_SRCREV_BY_PV "
                     "row (expected <PV>:<sha1>): %s" % row)
        if len(sha) != 40:
            bb.fatal("tensorflow-lite_%.bbappend: %s is not a 40-character SHA-1: %s"
                     % (pv, sha))
        table[pv] = sha

    pv = d.getVar("PV") or ""
    sha = table.get(pv)
    if not sha:
        bb.warn("tensorflow-lite %s is not listed in TFLITE_SRCREV_BY_PV, so it keeps "
                "SRCREV = ${AUTOREV} and parsing it needs `git ls-remote` to %s. "
                "Run `git ls-remote %s <branch>`, add the SHA to %s and parse again."
                % (pv, codelinaro, codelinaro,
                   d.getVar("FILE") or "tensorflow-lite_%.bbappend"))
        return

    # Plain SRCREV, not SRCREV:pn-...: see the header.  A local.conf
    # SRCREV:pn-tensorflow-lite would still take precedence over this, because
    # override variables win over the plain one -- that is intended, it lets a
    # user pin something else without editing the layer.
    d.setVar("SRCREV", sha)
}
