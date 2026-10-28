"""Stage cross-built C drivers into an explicitly selected disposable test image."""
from rootfs import stage as stage_system


def stage(disk):
    stage_system(disk)
