# -*- Python -*-
"""Yocto rule for yocto compiler autoconfiguration."""

def _tpl(repository_ctx, tpl, substitutions={}, out=None):
  if not out:
    out = tpl
  repository_ctx.template(
      out,
      Label("//third_party/toolchains/yocto:%s.tpl" % tpl),
      substitutions)

def _yocto_compiler_configure_impl(repository_ctx):
    if "CT_NAME" in repository_ctx.os.environ:
        cross_tool_name = repository_ctx.os.environ["CT_NAME"]
    else:
        cross_tool_name = "x86_64-wrs-linux"

    _tpl(repository_ctx, "cc_config.bzl", {
        "%{YOCTO_COMPILER_PATH}%": str(repository_ctx.path(
            repository_ctx.attr.remote_config_repo,
        )),
        "%{CT_NAME}%": cross_tool_name,
    })
    repository_ctx.symlink(repository_ctx.attr.build_file, "BUILD")

yocto_compiler_configure = repository_rule(
    implementation = _yocto_compiler_configure_impl,
    attrs = {
        "remote_config_repo": attr.string(mandatory = False, default =""),
        "build_file": attr.label(),
    },
)
