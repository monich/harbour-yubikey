TEMPLATE = subdirs
SUBDIRS = app zbar

app.file = app.pro
app.depends = zbar-target

zbar.file = zbar/zbar.pro
zbar.target = zbar-target

OTHER_FILES += LICENSE README.md rpm/*.spec
