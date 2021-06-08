set(COMPONENT_ADD_INCLUDEDIRS include)
set(COMPONENT_SRCS "webserver.c")
set(COMPONENT_REQUIRES "spiffs" "esp_http_server")
set(COMPONENT_EMBED_FILES   "../../front/build/favicon.ico" 
                            "../../front/build/index.html" 
                            "../../front/build/style.css" 
                            "../../front/build/Ubuntu.woff2"
                            "../../front/build/robots.txt"
                            )
register_component()
