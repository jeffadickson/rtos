project = "RISCue RTOS"
author = "RISCue RTOS contributors"
version = "0.2"
release = "0.2.0"
extensions = []
master_doc = "index"
html_theme = "alabaster"
html_title = "RISCue RTOS User Manual"
exclude_patterns = ["_build"]
nitpicky = True
latex_documents = [("index", "riscue-rtos-manual.tex", "RISCue RTOS User Manual", author, "manual")]

# External C typedefs, macro and namespace are not separate documented API objects.
# Project-owned types/functions remain checked by nitpicky mode.
nitpick_ignore = [("cpp:identifier", name) for name in
                  ("rtos", "uint8_t", "uint32_t", "size_t", "RTOS_TICK_HZ")]
