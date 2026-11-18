cmake_minimum_required(VERSION 3.24)
project(ModEditor VERSION 1.0.2 LANGUAGES CXX)   # the version itself: src/app/version.h

# PLAN.md §4.6: C++20, Qt 6.11.2 Core/Gui/Widgets only, warnings fixed not silenced
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
# slots / signals / emit are plain names (Python's oob.slots …): the window code uses Q_SLOTS / Q_SIGNALS / Q_EMIT
add_compile_definitions(QT_NO_KEYWORDS)

find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Core Gui Widgets)

option(MED_ASAN "AddressSanitizer build (a separate build folder, checks only)" OFF)
if(MSVC AND MED_ASAN)
    add_compile_options(/fsanitize=address /Zi)
    add_link_options(/INCREMENTAL:NO /DEBUG)
endif()
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /EHsc /Zc:__cplusplus)
    # release: optimise for speed, link-time optimisation, strip unused / merge identical functions
    add_compile_options($<$<CONFIG:Release>:/O2> $<$<CONFIG:Release>:/GL> $<$<CONFIG:Release>:/Gy> $<$<CONFIG:Release>:/Gw>)
    add_link_options($<$<CONFIG:Release>:/LTCG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()

# the five parts (PLAN.md §4.1): grid < core < map < ui < app. They fill up phase by phase; until a part has
# sources it is an interface library so the dependency order is fixed from the start.
add_library(med_grid STATIC
    src/grid/random.cpp)
target_include_directories(med_grid PUBLIC src)

add_library(med_core STATIC
    src/core/adjacency.cpp
    src/core/applyjournal.cpp
    src/core/recovery.cpp
    src/core/snapio.cpp
    src/core/blockcode.cpp
    src/core/blocks.cpp
    src/core/cbcheck.cpp
    src/core/culturefile.cpp
    src/core/countryhist.cpp
    src/core/diplomacy.cpp
    src/core/document.cpp
    src/core/events.cpp
    src/core/eventtemplates.cpp
    src/core/entryview_data.cpp
    src/core/eventtools.cpp
    src/core/fs.cpp
    src/core/history.cpp
    src/core/provbuild.cpp
    src/core/issuepanel_data.cpp
    src/core/importer.cpp
    src/core/localisation.cpp
    src/core/mapfiles.cpp
    src/core/maplook.cpp
    src/core/mapmodes.cpp
    src/core/moddata.cpp
    src/core/modcopy.cpp
    src/core/modfolders.cpp
    src/core/modrename.cpp
    src/core/modifiers.cpp
    src/core/oob.cpp src/core/traits.cpp
    src/core/popcaps.cpp
    src/core/popcheck.cpp
    src/core/popcase.cpp
    src/core/popfacts.cpp
    src/core/popsim.cpp
    src/core/popplace.cpp
    src/core/popinfluence.cpp
    src/core/positions.cpp
    src/core/production.cpp
    src/core/prodtree.cpp
    src/core/provrenumber.cpp
    src/core/pyjson.cpp
    src/core/pydifflib.cpp
    src/core/pypath.cpp
    src/core/pynum.cpp
    src/core/pytext.cpp
    src/core/refs.cpp
    src/core/reid.cpp
    src/core/script.cpp
    src/core/scriptstore.cpp
    src/core/shortcutfacts.cpp
    src/core/shortcutuse.cpp
    src/core/staterefs.cpp
    src/core/submods.cpp
    src/core/techdata.cpp)
target_include_directories(med_core PUBLIC src)
target_link_libraries(med_core PUBLIC med_grid Qt6::Core Qt6::Gui)   # (Gui: QTextDocument for scriptstore)

add_library(med_map STATIC
    src/map/adjacency_geo.cpp
    src/map/autofix.cpp
    src/map/ddsimage.cpp
    src/map/iconstrips_data.cpp
    src/map/imgio.cpp
    src/map/importer.cpp
    src/map/layerbmp.cpp
    src/map/layertools.cpp
    src/map/mapcheck.cpp
    src/map/mapsize.cpp
    src/map/colormap.cpp
    src/map/canals.cpp
    src/map/fourk.cpp
    src/map/mapmarks.cpp
    src/map/checksum.cpp
    src/map/focuswindow.cpp
    src/map/gamefont.cpp
    src/map/gamecam.cpp
    src/map/mismatch.cpp
    src/map/snapio_map.cpp
    src/map/mapview_data.cpp
    src/map/moreviews_data.cpp
    src/map/nationextras_data.cpp
    src/map/newprov.cpp
    src/map/provpanel_data.cpp
    src/map/pospanel_data.cpp
    src/map/positions_geo.cpp
    src/map/provbmp.cpp
    src/map/provdelete.cpp
    src/map/regionimport.cpp)
target_link_libraries(med_map PUBLIC med_core Qt6::Gui)

add_library(med_ui STATIC
    src/ui/checkpanel.cpp
    src/ui/colorwheel.cpp
    src/ui/maplook.cpp
    src/ui/mapview.cpp
    src/ui/mismatchbox.cpp
    src/ui/modepanel.cpp
    src/ui/rail.cpp
    src/ui/railicons.cpp
    src/ui/terrainpanel.cpp
    src/ui/riverpanel.cpp
    src/ui/colormappanel.cpp
    src/ui/newprov_dialog.cpp
    src/ui/pospanel.cpp
    src/ui/drawpanel.cpp
    src/ui/provclip.cpp
    src/ui/adjpanel.cpp
    src/ui/canalpanel.cpp
    src/ui/mapdefs.cpp
    src/ui/regionimport.cpp
    src/ui/mapsize.cpp
    src/ui/undokeys.cpp
    src/ui/lookpages.cpp
    src/ui/mapmarkspage.cpp
    src/ui/gameview.cpp
    src/ui/importpanel.cpp
    src/ui/submodpage.cpp
    src/ui/checksumpage.cpp
    src/ui/culturewin.cpp
    src/ui/econest.cpp
    src/ui/popinfluence.cpp
    src/ui/palette.cpp
    src/ui/dialogs.cpp
    src/ui/panels.cpp
    src/ui/popfixbox.cpp
    src/ui/provpanel.cpp
    src/ui/statepanel.cpp
    src/ui/buildtypes.cpp
    src/ui/utilpanel.cpp
    src/ui/colsizes.cpp
    src/ui/nationextras.cpp
    src/ui/nationpanel.cpp src/ui/partieswin.cpp src/ui/flagstudio.cpp src/ui/leaderbox.cpp src/ui/leaderlist.cpp src/ui/traitodds.cpp src/ui/traitnames.cpp src/ui/armypanel.cpp
    src/ui/diplopanel.cpp
    src/ui/diploweb.cpp
    src/ui/locwin.cpp src/ui/shortcuts.cpp src/ui/reformwin.cpp src/ui/modpanel.cpp src/ui/issuepanel.cpp
    src/ui/defpanel.cpp src/ui/entryview.cpp src/ui/politicspanel.cpp src/ui/techpanel.cpp
    src/ui/prodpanel.cpp
    src/ui/unitpanel.cpp
    src/ui/moreviews.cpp
    src/ui/unittimeline.cpp
    src/ui/stockpilepage.cpp
    src/ui/iconstrips.cpp
    src/ui/relview.cpp
    src/ui/relgraph.cpp
    src/ui/scriptsearch.cpp
    src/ui/scriptedit.cpp
    src/ui/cbpanel.cpp
    src/ui/eventpanel.cpp
    src/ui/scriptpage.cpp
    src/ui/iconboard.cpp
    src/ui/reidwin.cpp
    src/ui/eventnet.cpp
    src/ui/bigedit.cpp
    src/ui/nodegraph.cpp
    src/ui/blockshapes.cpp
    src/ui/blockbuilder.cpp
    src/ui/changelog.cpp
    src/ui/textselect.cpp
    src/ui/theme.cpp
    src/ui/widgets.cpp)
target_link_libraries(med_ui PUBLIC med_map Qt6::Widgets)

add_library(med_app STATIC
    src/app/app.cpp
    src/app/mainwindow.cpp
    src/app/update.cpp)
target_link_libraries(med_app PUBLIC med_ui winhttp)   # winhttp: the update check (Windows' own, no Qt module)

add_executable(ModEditor WIN32 src/app/main.cpp src/app/modeditor.rc)   # .rc: the version block
target_link_libraries(ModEditor PRIVATE med_app)

# --- checks (console programs; never shipped)
add_executable(med_g1 tests/core/g1.cpp)              # G1: fingerprints of every parsed file
target_link_libraries(med_g1 PRIVATE med_core)
add_executable(med_pynum tests/core/pynum.cpp)        # Python's number rules, against Python's own output
target_link_libraries(med_pynum PRIVATE med_core)
add_executable(med_fs tests/core/fs_check.cpp)        # file operations + dict order, against Python
target_link_libraries(med_fs PRIVATE med_core)
add_executable(med_eventtemplates_check tests/core/eventtemplates_check.cpp)   # eventtemplates: G2 against Python
target_link_libraries(med_eventtemplates_check PRIVATE med_core)
add_executable(med_g2_moddata tests/core/g2_moddata.cpp)   # G2: what Mod holds, against Python
target_link_libraries(med_g2_moddata PRIVATE med_core)
add_executable(med_g4_moddata tests/core/g4_moddata.cpp)   # G4: moddata's writers, against Python
target_link_libraries(med_g4_moddata PRIVATE med_core)
add_executable(med_history tests/core/history_check.cpp)   # history: G2 + G4 against Python
target_link_libraries(med_history PRIVATE med_core)
add_executable(med_mapfiles tests/core/mapfiles_check.cpp)   # mapfiles: G2 + G4 against Python
target_link_libraries(med_mapfiles PRIVATE med_core)
add_executable(med_adjacency tests/core/adjacency_check.cpp)   # adjacency: G2 + G4 against Python
target_link_libraries(med_adjacency PRIVATE med_core)
add_executable(med_countryhist tests/core/countryhist_check.cpp)   # countryhist: G2 + G4 against Python
target_link_libraries(med_countryhist PRIVATE med_core)
add_executable(med_diplomacy tests/core/diplomacy_check.cpp)   # diplomacy: G2 + G4 against Python
target_link_libraries(med_diplomacy PRIVATE med_core)
add_executable(med_events tests/core/events_check.cpp)   # events: G2 against Python
target_link_libraries(med_events PRIVATE med_core)
add_executable(med_modifiers tests/core/modifiers_check.cpp)   # modifiers: G2 against Python
target_link_libraries(med_modifiers PRIVATE med_core)
add_executable(med_oob tests/core/oob_check.cpp)   # oob: G2 + G4 against Python
target_link_libraries(med_oob PRIVATE med_core)
add_executable(med_techprod tests/core/techprod_check.cpp)   # techdata + production: G2 + G4 against Python
target_link_libraries(med_techprod PRIVATE med_core)
add_executable(med_cbcheck tests/core/cbcheck_check.cpp)   # cbcheck + difflib: G2 against Python
target_link_libraries(med_cbcheck PRIVATE med_core)
add_executable(med_refs tests/core/refs_check.cpp)   # refs + popcaps: G2 against Python
target_link_libraries(med_refs PRIVATE med_core)
add_executable(med_blocks tests/core/blocks_check.cpp)   # blocks + blockcode: G2 against Python
target_link_libraries(med_blocks PRIVATE med_core)
add_executable(med_eventtools tests/core/eventtools_check.cpp)   # eventtools: G2 against Python
target_link_libraries(med_eventtools PRIVATE med_core)
add_executable(med_provrenumber tests/core/provrenumber_check.cpp)   # provrenumber: G2 + G4 against Python
target_link_libraries(med_provrenumber PRIVATE med_core)
add_executable(med_popsim tests/core/popsim_check.cpp)   # popfacts + popcase + popsim: G2 against Python
target_link_libraries(med_popsim PRIVATE med_core)
add_executable(med_shortcuts tests/core/shortcuts_check.cpp)   # shortcutfacts + shortcutuse: G2 against Python
target_link_libraries(med_shortcuts PRIVATE med_core)
add_executable(med_submods tests/core/submods_check.cpp)   # submods: G2 + G4 against Python
target_link_libraries(med_submods PRIVATE med_core)
add_executable(med_staterefs tests/core/staterefs_check.cpp)   # staterefs: G2 against Python
target_link_libraries(med_staterefs PRIVATE med_core)
add_executable(med_prodtree tests/core/prodtree_check.cpp)   # prodtree + py::hypot: G2 against Python
target_link_libraries(med_prodtree PRIVATE med_core)
add_executable(med_mapmodes tests/core/mapmodes_check.cpp)   # mapmodes (window-less half): G2 against Python
target_link_libraries(med_mapmodes PRIVATE med_map)
add_executable(med_popcheck tests/core/popcheck_check.cpp)   # popcheck + importer.find_sources: G2 against Python
target_link_libraries(med_popcheck PRIVATE med_core)
add_executable(med_random tests/grid/random_check.cpp)   # grid/random: numpy's default_rng, number for number
target_link_libraries(med_random PRIVATE med_grid Qt6::Core)
add_executable(med_pyset tests/grid/pyset_check.cpp)   # grid/pyset.h: CPython's set order, against Python
target_link_libraries(med_pyset PRIVATE med_grid Qt6::Core)
add_executable(med_imgio tests/map/imgio_check.cpp)   # imgio + ddsimage: G2 + G4 against Python
target_link_libraries(med_imgio PRIVATE med_map)
add_executable(med_mapdata tests/map/mapdata_check.cpp)   # MapData (mapview's data half): G2 against Python
target_link_libraries(med_mapdata PRIVATE med_map)
add_executable(med_layertools tests/map/layertools_check.cpp)   # layertools + grid/npmath: against numpy
target_link_libraries(med_layertools PRIVATE med_map)
add_executable(med_mapbmps tests/map/mapbmps_check.cpp)   # layerbmp + provbmp + province_pieces: G2 + G4 against Python
target_link_libraries(med_mapbmps PRIVATE med_map)
add_executable(med_adjgeo tests/map/adjgeo_check.cpp)   # adjacency's map half: G2 against Python
target_link_libraries(med_adjgeo PRIVATE med_map)
add_executable(med_positions tests/map/positions_check.cpp)   # positions (file + placement): G2 + G4 against Python
target_link_libraries(med_positions PRIVATE med_map)
add_executable(med_newprov tests/map/newprov_check.cpp)   # newprov's data half: G2 + G4 against Python
target_link_libraries(med_newprov PRIVATE med_map)
add_executable(med_mapcheck tests/map/mapcheck_check.cpp)   # mapcheck.run_checks: G2 against Python
target_link_libraries(med_mapcheck PRIVATE med_map)
add_executable(med_autofix tests/map/autofix_check.cpp)   # autofix: G2 against Python
target_link_libraries(med_autofix PRIVATE med_map)
add_executable(med_provdelete tests/map/provdelete_check.cpp)   # provdelete: G2 + G4 against Python
target_link_libraries(med_provdelete PRIVATE med_map)
add_executable(med_mapsize tests/map/mapsize_check.cpp)   # mapsize's cut: G2 + G4 against Python
target_link_libraries(med_mapsize PRIVATE med_map)
add_executable(med_colormap_check tests/map/colormap_check.cpp)   # Draw → Colours: maths + files (C++ only)
target_link_libraries(med_colormap_check PRIVATE med_map)
add_executable(med_drawperf_check tests/ui/drawperf_check.cpp)   # the Drawing window's timings (C++ only)
target_link_libraries(med_drawperf_check PRIVATE med_app)
add_executable(med_undo_check tests/ui/undo_check.cpp)   # Ctrl+Z / Ctrl+Y one change at a time, newest first (C++ only)
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Test)   # (QTest's real key presses: this check only, not the editor)
target_link_libraries(med_undo_check PRIVATE med_app Qt6::Test)
add_executable(med_promo tests/ui/promo_shots.cpp)   # promotion pictures: the main window driven by a command file
target_link_libraries(med_promo PRIVATE med_app)
add_executable(med_pageperf_check tests/ui/pageperf_check.cpp)   # every page's / map mode's timings (C++ only)
target_link_libraries(med_pageperf_check PRIVATE med_app)
add_executable(med_colormapfit tests/map/colormapfit_check.cpp)   # colour maps: cut in their format, fit (C++ only)
target_link_libraries(med_colormapfit PRIVATE med_map)
add_executable(med_importer tests/map/importer_check.cpp)   # importer: G2 + G4 against Python
target_link_libraries(med_importer PRIVATE med_map)
add_executable(med_applyjournal tests/core/applyjournal_check.cpp)   # applyjournal: G2 + G4 against Python
target_link_libraries(med_applyjournal PRIVATE med_core)
add_executable(med_mismatch tests/map/mismatch_check.cpp)   # mismatch (land / sea between the three maps): G2
target_link_libraries(med_mismatch PRIVATE med_map)
add_executable(med_recovery tests/map/recovery_check.cpp)   # recovery (picture, units, cut, put back): G4
target_link_libraries(med_recovery PRIVATE med_map)
add_executable(med_canals tests/map/canals_check.cpp)   # canals (models, sprites, openings, writers): G2
target_link_libraries(med_canals PRIVATE med_map)
add_executable(med_fourk tests/map/fourk_check.cpp)   # fourk (the 4K submod): G2 + G4 against Python
target_link_libraries(med_fourk PRIVATE med_map)
add_executable(med_gamesum tests/map/gamesum_check.cpp)   # checksum::predict against the game's own numbers (C++ only)
target_link_libraries(med_gamesum PRIVATE med_map)
add_executable(med_gamefont tests/map/gamefont_check.cpp)   # gamefont (text drawn as the game draws it): G2
target_link_libraries(med_gamefont PRIVATE med_map)
add_executable(med_focuswindow tests/map/focuswindow_check.cpp)   # focuswindow (the focus window fitted, drawn): G2
target_link_libraries(med_focuswindow PRIVATE med_map)
add_executable(med_replay tests/ops/replay.cpp)          # scripted sessions (tests/ops), against the oracle
target_link_libraries(med_replay PRIVATE med_core)
add_executable(med_widgets tests/ui/widgets_check.cpp)   # theme + textselect + widgets: G2 + G5 against Python
target_link_libraries(med_widgets PRIVATE med_ui)
add_executable(med_rail tests/ui/rail_check.cpp)   # railicons + rail: G2 + G5 against Python
target_link_libraries(med_rail PRIVATE med_ui)
add_executable(med_app_check tests/ui/app_check.cpp)   # app (start window, splash, icon): G2 + G5 against Python
target_link_libraries(med_app_check PRIVATE med_app)
add_executable(med_update_check tests/ui/update_check.cpp)   # update: versions compared, GitHub asked, the note + tick (C++ only)
target_link_libraries(med_update_check PRIVATE med_app)
add_executable(med_colorwheel_check tests/ui/colorwheel_check.cpp)   # colorwheel: G2 + G5 against Python
target_link_libraries(med_colorwheel_check PRIVATE med_ui)
add_executable(med_terrainpanel_check tests/ui/terrainpanel_check.cpp)   # terrainpanel + mismatchbox: G2 + G5
target_link_libraries(med_terrainpanel_check PRIVATE med_app)
add_executable(med_riverpanel_check tests/ui/riverpanel_check.cpp)   # riverpanel: G2 + G5
target_link_libraries(med_riverpanel_check PRIVATE med_app)
add_executable(med_pospanel_check tests/ui/pospanel_check.cpp)   # pospanel: G2 + G5
target_link_libraries(med_pospanel_check PRIVATE med_app)
add_executable(med_drawpanel_check tests/ui/drawpanel_check.cpp)   # drawpanel: G2 + G5
target_link_libraries(med_drawpanel_check PRIVATE med_app)
add_executable(med_modcopy_check tests/core/modcopy_check.cpp)   # modcopy: keep a copy of the original mod (C++ only)
target_link_libraries(med_modcopy_check PRIVATE med_app)
add_executable(med_newmod_check tests/ui/newmod_check.cpp)   # a new mod from the base game, opened + applied (C++ only)
target_link_libraries(med_newmod_check PRIVATE med_app)
add_executable(med_provbuild_check tests/core/provbuild_check.cpp)   # provbuild: new province buildings (C++ only)
target_link_libraries(med_provbuild_check PRIVATE med_map)
add_executable(med_buildtypes_check tests/ui/buildtypes_check.cpp)   # Building types page: new province buildings (C++ only)
target_link_libraries(med_buildtypes_check PRIVATE med_app)
add_executable(med_modrename_check tests/core/modrename_check.cpp)   # modrename: rename a mod (C++ only)
target_link_libraries(med_modrename_check PRIVATE med_app)
add_executable(med_modname_check tests/ui/modname_check.cpp)   # the mod's name: rename by double click (C++ only)
target_link_libraries(med_modname_check PRIVATE med_app)
add_executable(med_leaders_check tests/ui/leaders_check.cpp)   # Generals / Admirals + Leader traits odds (C++ only)
target_link_libraries(med_leaders_check PRIVATE med_app)
add_executable(med_modfolders_check tests/ui/modfolders_check.cpp)   # the opening window: base game, more mod folders, drags (C++ only)
target_link_libraries(med_modfolders_check PRIVATE med_app)
add_executable(med_provclip_check tests/ui/provclip_check.cpp)   # provclip: cut / paste provinces (C++ only)
target_link_libraries(med_provclip_check PRIVATE med_app)
add_executable(med_adjpanel_check tests/ui/adjpanel_check.cpp)   # adjpanel: G2 + G5
target_link_libraries(med_adjpanel_check PRIVATE med_app)
add_executable(med_canalpanel_check tests/ui/canalpanel_check.cpp)   # canalpanel: G2 + G5
target_link_libraries(med_canalpanel_check PRIVATE med_app)
add_executable(med_mapdefs_check tests/ui/mapdefs_check.cpp)   # mapdefs: G2 + G5
target_link_libraries(med_mapdefs_check PRIVATE med_app)
add_executable(med_regionimport_check tests/ui/regionimport_check.cpp)   # regionimport: G2 + G5
target_link_libraries(med_regionimport_check PRIVATE med_app)
add_executable(med_mapsizepage_check tests/ui/mapsizepage_check.cpp)   # mapsize's page: G2 + G5
target_link_libraries(med_mapsizepage_check PRIVATE med_app)
add_executable(med_maptab_check tests/ui/maptab_check.cpp)   # the Map tab + Map size page wired in: G2 + G5
target_link_libraries(med_maptab_check PRIVATE med_app)
add_executable(med_errfix_check tests/ui/errfix_check.cpp)   # the Errors tab's fixes in the window: G2 + G4
target_link_libraries(med_errfix_check PRIVATE med_app)
add_executable(med_gameview_check tests/ui/gameview_check.cpp)   # the game's screen drawn by the editor (C++ only)
target_link_libraries(med_gameview_check PRIVATE med_ui)
add_executable(med_lookpages_check tests/ui/lookpages_check.cpp)   # Library → Map look, Menu & loading: G2 + G4 + G5
target_link_libraries(med_lookpages_check PRIVATE med_app)
add_executable(med_popspage_check tests/ui/popspage_check.cpp)   # Population (pop editor, split, dialogs): G2 + G4 + G5
target_link_libraries(med_popspage_check PRIVATE med_app)
add_executable(med_provpage_check tests/ui/provpage_check.cpp)   # Provinces & states (provinces, utility, states): G2 + G4 + G5
target_link_libraries(med_provpage_check PRIVATE med_app)
add_executable(med_nationpage_check tests/ui/nationpage_check.cpp)   # Nation (nation page, parties / flags boxes): G2 + G4 + G5
target_link_libraries(med_nationpage_check PRIVATE med_app)
add_executable(med_partieswin_check tests/ui/partieswin_check.cpp)   # the parties window: G2 + G4 + G5
target_link_libraries(med_partieswin_check PRIVATE med_app)
add_executable(med_flagstudio_check tests/ui/flagstudio_check.cpp)   # the flag studio: G2 + G4 + G5
target_link_libraries(med_flagstudio_check PRIVATE med_app)
add_executable(med_armypage_check tests/ui/armypage_check.cpp)   # the Armies page: G2 + G4 + G5
target_link_libraries(med_armypage_check PRIVATE med_app)
add_executable(med_diplopage_check tests/ui/diplopage_check.cpp)   # the Diplomacy page: G2 + G4 + G5
target_link_libraries(med_diplopage_check PRIVATE med_app)
add_executable(med_diploweb_check tests/ui/diploweb_check.cpp)   # the Relations view (Diplomacy): G2 + G4 + G5
target_link_libraries(med_diploweb_check PRIVATE med_app)
add_executable(med_politics_check tests/ui/politics_check.cpp)   # the Politics tab: G2 + G4 + G5
target_link_libraries(med_politics_check PRIVATE med_app)
add_executable(med_tech_check tests/ui/tech_check.cpp)   # the Technology tab: G2 + G4 + G5
target_link_libraries(med_tech_check PRIVATE med_app)
add_executable(med_prod_check tests/ui/prod_check.cpp)   # the Economy tab: G2 + G4 + G5
target_link_libraries(med_prod_check PRIVATE med_app)
add_executable(med_warfare_check tests/ui/warfare_check.cpp)   # the Warfare tab: G2 + G4 + G5
target_link_libraries(med_warfare_check PRIVATE med_app)
add_executable(med_rules_check tests/ui/rules_check.cpp)   # the Game rules tab (step A): G2 + G4 + G5
target_link_libraries(med_rules_check PRIVATE med_app)
add_executable(med_library_check tests/ui/library_check.cpp)   # the Library tab: G2 + G4 + G5
target_link_libraries(med_library_check PRIVATE med_app)
add_executable(med_import_check tests/ui/import_check.cpp)   # the Import tab: G2 + G4 + G5
target_link_libraries(med_import_check PRIVATE med_app)
add_executable(med_keyspage_check tests/ui/keyspage_check.cpp)   # Rules -> Shortcuts: G2 + G4 + G5
target_link_libraries(med_keyspage_check PRIVATE med_app)
add_executable(med_submodpage_check tests/ui/submodpage_check.cpp)   # Tools -> Submods & 4K: G2 + G4
target_link_libraries(med_submodpage_check PRIVATE med_app)
add_executable(med_culturewin_check tests/ui/culturewin_check.cpp)   # Population -> Edit cultures: G2
target_link_libraries(med_culturewin_check PRIVATE med_app)
add_executable(med_econest_check tests/ui/econest_check.cpp)   # Economy -> Output estimate: G2
target_link_libraries(med_econest_check PRIVATE med_app)
add_executable(med_popinfluence_check tests/ui/popinfluence_check.cpp)   # Pop influences: G2
target_link_libraries(med_popinfluence_check PRIVATE med_app)
add_executable(med_palette_check tests/ui/palette_check.cpp)   # the command palette: G2, G5
target_link_libraries(med_palette_check PRIVATE med_app)
add_executable(med_scripts_check tests/ui/scripts_check.cpp)   # the Scripts tab: G2 + G4 + G5
target_link_libraries(med_scripts_check PRIVATE med_app)
add_executable(med_reid_check tests/ui/reid_check.cpp)   # Scripts -> IDs (the ID system, Re-ID): G2 + G4 + G5
target_link_libraries(med_reid_check PRIVATE med_app)
add_executable(med_eventnet_check tests/ui/eventnet_check.cpp)   # Scripts -> the event network: G2 + G5
target_link_libraries(med_eventnet_check PRIVATE med_app)
add_executable(med_bigedit_check tests/ui/bigedit_check.cpp)   # list pages -> the large window: G2 + G5
target_link_libraries(med_bigedit_check PRIVATE med_app)
add_executable(med_locwin_check tests/ui/locwin_check.cpp)   # Scripts -> Texts & preview: G2 + G5
target_link_libraries(med_locwin_check PRIVATE med_app)
add_executable(med_nodegraph_check tests/ui/nodegraph_check.cpp)   # Scripts -> Nodes: G2 + G5
target_link_libraries(med_nodegraph_check PRIVATE med_app)
add_executable(med_blockbuilder_check tests/ui/blockbuilder_check.cpp)   # Scripts -> Blocks: G2 + G5
target_link_libraries(med_blockbuilder_check PRIVATE med_app)
add_executable(med_changelog_check tests/ui/changelog_check.cpp)   # Changelog: G2 + G5
target_link_libraries(med_changelog_check PRIVATE med_app)
add_executable(med_mainwindow_check tests/ui/mainwindow_check.cpp)   # mainwindow (frame, map modes, Errors): G2 + G5
target_link_libraries(med_mainwindow_check PRIVATE med_app)
add_executable(med_maxwatch_check tests/ui/maxwatch_check.cpp)   # (C++ only) the main window stays maximised on every page (real screen)
target_link_libraries(med_maxwatch_check PRIVATE med_app)
add_executable(med_mainnav_check tests/ui/mainnav_check.cpp)   # mainwindow: search, problem line, jumps, files-changed bar: G2
target_link_libraries(med_mainnav_check PRIVATE med_app)
add_executable(med_subwin_check tests/ui/subwin_check.cpp)   # mainwindow on a submod: the strip, _submod_sync: G2
target_link_libraries(med_subwin_check PRIVATE med_app)
add_executable(med_mapview tests/ui/mapview_check.cpp)   # mapview (the map widget): G2 + G5 against Python
target_link_libraries(med_mapview PRIVATE med_ui)
