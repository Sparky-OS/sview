project (StTests)

set (USED_SRCFILES
  main.cpp
  StTestEmbed.cpp
  StTestGlBand.cpp
  StTestGlStress.cpp
  StTestImageLib.cpp
  StTestMutex.cpp
)
set (USED_MMFILES
  main.mm
  StTestEmbed.ObjC.mm
)

set (USED_INCFILES
  StTest.h
  StTestEmbed.h
  StTestGlBand.h
  StTestGlStress.h
  StTestImageLib.h
  StTestMutex.h
  StTestResponder.h
)

set (USED_MANFILES "")
set (USED_RESFILES "")
if (WIN32)
  set (USED_RESFILES "StTests.rc")
  set (USED_MANFILES "../adm/cmake/dpiAware.manifest")
endif()

if (NOT APPLE)
  foreach (aFileIter ${USED_MMFILES})
    set_source_files_properties (${aFileIter} PROPERTIES HEADER_FILE_ONLY TRUE)
  endforeach()
endif()

source_group ("Source Files"   FILES ${USED_SRCFILES} ${USED_MMFILES})
source_group ("Header Files"   FILES ${USED_INCFILES})
source_group ("Resource Files" FILES ${USED_RESFILES} ${USED_MANFILES})

# library to build
add_executable (${PROJECT_NAME}
  ${USED_SRCFILES} ${USED_MMFILES} ${USED_INCFILES} ${USED_RESFILES} ${USED_MANFILES}
)

set_target_properties (${PROJECT_NAME} PROPERTIES FOLDER "Tools")
st_set_target_output_dirs(${PROJECT_NAME})

# internal dependencies
set (aDeps StCore StShared)
foreach (aDepIter ${aDeps})
  add_dependencies (${PROJECT_NAME} ${aDepIter})
  target_link_libraries (${PROJECT_NAME} PRIVATE ${aDepIter})
endforeach()

# external dependencies
target_link_libraries (${PROJECT_NAME} PRIVATE avutil)
if (USE_XLIB)
  target_link_libraries (${PROJECT_NAME} PRIVATE X11::Xrandr X11::Xext X11::Xpm X11::X11)
endif()
target_link_libraries (${PROJECT_NAME} PRIVATE ${ST_OPENGL_LIBRARIES})
if (APPLE)
  target_link_libraries (${PROJECT_NAME} PRIVATE "-framework CoreFoundation" "-framework Appkit" objc)
endif()

add_executable(StStereoUnpackTest testStereoUnpack.cpp)
target_link_libraries(StStereoUnpackTest PRIVATE StShared avutil)
add_test(NAME stereo-unpack COMMAND StStereoUnpackTest)
add_executable(StRecentFileStateTest testRecentFileState.cpp)
target_link_libraries(StRecentFileStateTest PRIVATE StShared)
add_test(NAME recent-file-state COMMAND StRecentFileStateTest)
add_executable(StSparseStereoTest testSparseStereo.cpp)
target_link_libraries(StSparseStereoTest PRIVATE StShared avutil)
add_test(NAME sparse-stereo COMMAND StSparseStereoTest)
add_executable(StJpsDescriptorTest testJpsDescriptor.cpp)
target_link_libraries(StJpsDescriptorTest PRIVATE StShared)
add_test(NAME jps-descriptor COMMAND StJpsDescriptorTest)
add_executable(StLanguageEnvironmentTest testLanguageEnvironment.cpp)
target_link_libraries(StLanguageEnvironmentTest PRIVATE StShared)
add_test(NAME language-environment COMMAND StLanguageEnvironmentTest)
add_executable(StPngStereoTest testPngStereo.cpp)
target_link_libraries(StPngStereoTest PRIVATE StShared avutil)
add_test(NAME png-ster COMMAND StPngStereoTest)
add_executable(StCorrectionTransactionTest testCorrectionTransaction.cpp)
target_link_libraries(StCorrectionTransactionTest PRIVATE StShared)
target_compile_definitions(StCorrectionTransactionTest PRIVATE CORRECTION_DATA="${CMAKE_CURRENT_SOURCE_DIR}/corrections")
add_test(NAME correction-transaction COMMAND StCorrectionTransactionTest)
add_executable(StSaveLanguagesTest testSaveLanguages.cpp)
target_compile_definitions(StSaveLanguagesTest PRIVATE SVIEW_SOURCE="${CMAKE_SOURCE_DIR}")
add_test(NAME save-languages COMMAND StSaveLanguagesTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(StVideoCorrectionTest testVideoCorrection.cpp
  ../StMoviePlayer/StVideo/StVideoStereoMark.cpp ../StMoviePlayer/StVideo/StVideoQueue.cpp)
target_compile_options(StVideoCorrectionTest PRIVATE -ffunction-sections -fdata-sections)
target_link_options(StVideoCorrectionTest PRIVATE -Wl,--gc-sections)
target_link_libraries(StVideoCorrectionTest PRIVATE StShared avformat avcodec avutil swscale)
add_test(NAME video-correction COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/videoCorrection.py $<TARGET_FILE:StVideoCorrectionTest>)
add_executable(StMpoLoaderTest testMpoLoader.cpp ../StImageViewer/StImageLoader.cpp
  ../StImageViewer/StImageViewerGUI.cpp ../StImageViewer/StImageViewerStrings.cpp)
target_compile_options(StMpoLoaderTest PRIVATE -ffunction-sections -fdata-sections)
target_link_options(StMpoLoaderTest PRIVATE -Wl,--gc-sections)
target_link_libraries(StMpoLoaderTest PRIVATE StShared StCore StGLWidgets avutil freetype ${ST_OPENGL_LIBRARIES})
target_compile_definitions(StMpoLoaderTest PRIVATE CORRECTION_DATA="${CMAKE_CURRENT_SOURCE_DIR}/corrections")
add_test(NAME mpo-loader-save COMMAND StMpoLoaderTest)
