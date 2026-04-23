project = "platform-nes"
author = "Brette"
release = "0.1"

extensions = ["breathe"]

breathe_projects = {
    "api":      "./doxygen-api/xml",
    "advanced": "./doxygen-advanced/xml",
}
breathe_default_project = "api"

exclude_patterns = ["_build", "Thumbs.db", ".DS_Store", "doxygen-api", "doxygen-advanced"]

# Breathe emits identical declarations from both Doxygen projects (headers are
# shared). Sphinx's C++/C domains warn on those duplicates even though they're
# deliberate; silence the noise.
suppress_warnings = [
    "duplicate_declaration.cpp",
    "duplicate_declaration.c",
]

html_theme = "sphinx_rtd_theme"
html_static_path = []
