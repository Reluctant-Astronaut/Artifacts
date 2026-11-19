# The five plugins of the suite, and the sources each one compiles besides the shared editor
set(STRIP_SOURCES  ${CMAKE_CURRENT_LIST_DIR}/Strip/StripProcessor.cpp)

function(artifacts_add TARGET PRODUCT CODE VARIANT SOURCES)
    artifacts_plugin(${TARGET} "${PRODUCT}" ${CODE} ${VARIANT} ${ARGN})
    target_sources(${TARGET} PRIVATE ${SOURCES})
    if(ARTIFACTS_TESTS)
        target_sources(${TARGET}Tests PRIVATE ${SOURCES})
        target_include_directories(${TARGET}Tests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/Strip ${CMAKE_CURRENT_LIST_DIR}/Duality ${CMAKE_CURRENT_LIST_DIR}/Anemoia ${CMAKE_CURRENT_LIST_DIR}/Equanimity)
    endif()
    target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/Strip ${CMAKE_CURRENT_LIST_DIR}/Duality ${CMAKE_CURRENT_LIST_DIR}/Anemoia ${CMAKE_CURRENT_LIST_DIR}/Equanimity)
endfunction()

artifacts_add(Console  "Console RA-1" ArC1 2 "${STRIP_SOURCES}" Dynamics)
artifacts_add(Kintsugi "Kintsugi"     ArK1 1 "${STRIP_SOURCES}" Distortion)

set(DUALITY_SOURCES ${CMAKE_CURRENT_LIST_DIR}/Duality/DualityProcessor.cpp)
artifacts_add(Duality "Duality" ArD1 3 "${DUALITY_SOURCES}" Reverb)

set(ANEMOIA_SOURCES ${CMAKE_CURRENT_LIST_DIR}/Anemoia/AnemoiaProcessor.cpp)
artifacts_add(Anemoia "Anemoia" ArA1 4 "${ANEMOIA_SOURCES}" Delay)

set(EQUANIMITY_SOURCES ${CMAKE_CURRENT_LIST_DIR}/Equanimity/EquanimityProcessor.cpp)
artifacts_add(Equanimity "Equanimity" ArE1 5 "${EQUANIMITY_SOURCES}" EQ)
