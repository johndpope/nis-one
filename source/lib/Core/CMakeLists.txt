cmake_minimum_required ( VERSION 2.8 )

project ( NiSCore )

#set ( CMAKE_MACOSX_RPATH 1 )

aux_source_directory ( . Core_SOURCE_FILES )

# settings for using Qt5
set ( CMAKE_INCLUDE_CURRENT_DIR ON )
set ( CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} "/usr/local/Qt5" )

find_package ( Qt5Core REQUIRED )
find_package ( Qt5OpenGL REQUIRED )
find_package ( OpenCV REQUIRED )
find_package ( Boost COMPONENTS system filesystem serialization iostreams REQUIRED )

include_directories ( ${OpenCV_INCLUDE_DIRS} ${Boost_INCLUDE_DIR} ${NiS_INCLUDE_DIR} )

add_library ( NiSCore SHARED ${Core_SOURCE_FILES} )
target_link_libraries ( NiSCore
	${OpenCV_LIBS}
	${Boost_LIBRARIES}
	Qt5::Core
	Qt5::OpenGL )

install ( TARGETS
	NiSCore
	DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" )