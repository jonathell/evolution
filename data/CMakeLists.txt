set(filedeps)
set(desktopdir ${SHARE_INSTALL_PREFIX}/applications)
set(themedir ${privdatadir}/theme)

configure_file(org.gnome.Evolution.desktop.in.in
	org.gnome.Evolution.desktop.in
	@ONLY
)

intltool_merge(${CMAKE_CURRENT_BINARY_DIR}/org.gnome.Evolution.desktop.in org.gnome.Evolution.desktop --desktop-style --utf8)

list(APPEND filedeps ${CMAKE_CURRENT_BINARY_DIR}/org.gnome.Evolution.desktop)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.gnome.Evolution.desktop
	DESTINATION ${desktopdir}
)

add_appdata_file(org.gnome.Evolution.appdata.xml.in org.gnome.Evolution.appdata.xml)

install(FILES webview.css
		webview-print.css
	DESTINATION ${themedir}
)

set(SCHEMAS
	org.gnome.evolution.gschema.xml
	org.gnome.evolution.addressbook.gschema.xml
	org.gnome.evolution.calendar.gschema.xml
	org.gnome.evolution.importer.gschema.xml
	org.gnome.evolution.mail.gschema.xml
	org.gnome.evolution.shell.gschema.xml
	org.gnome.evolution.bogofilter.gschema.xml
	org.gnome.evolution.spamassassin.gschema.xml
	org.gnome.evolution.text-highlight.gschema.xml
	org.gnome.evolution.plugin.attachment-reminder.gschema.xml
	org.gnome.evolution.plugin.autocontacts.gschema.xml
	org.gnome.evolution.plugin.email-custom-header.gschema.xml
	org.gnome.evolution.plugin.external-editor.gschema.xml
	org.gnome.evolution.plugin.face-picture.gschema.xml
	org.gnome.evolution.plugin.itip.gschema.xml
	org.gnome.evolution.plugin.mail-notification.gschema.xml
	org.gnome.evolution.plugin.prefer-plain.gschema.xml
	org.gnome.evolution.plugin.publish-calendar.gschema.xml
	org.gnome.evolution.plugin.templates.gschema.xml
)

set(BUILT_SCHEMAS)
foreach(_schema IN LISTS SCHEMAS)
	intltool_merge(${_schema}.in ${_schema} --no-translations --xml-style --utf8)
	list(APPEND BUILT_SCHEMAS ${CMAKE_CURRENT_BINARY_DIR}/${_schema})
endforeach(_schema)

add_custom_command(OUTPUT gschemas.compiled
	COMMAND ${GLIB_COMPILE_SCHEMAS} .
)

add_custom_target(data-files ALL
	DEPENDS gschemas.compiled
		${filedeps}
)

add_gsettings_schemas(data-files ${BUILT_SCHEMAS})

install(FILES evolution.convert
	DESTINATION ${SHARE_INSTALL_PREFIX}/GConf/gsettings
)

add_subdirectory(icons)
add_subdirectory(images)
add_subdirectory(sounds)
add_subdirectory(ui)
add_subdirectory(views)
