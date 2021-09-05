set(COMPONENT_ADD_INCLUDEDIRS include)
set(COMPONENT_SRCS "webserver.c")
set(COMPONENT_REQUIRES "spiffs" "esp_http_server")
set(COMPONENT_EMBED_FILES   "../../front/public/bundle.css" 
                            "../../front/public/bundle.js" 
                            "../../front/public/bundle.js.map"
                            "../../front/public/global.css"
                            "../../front/public/index.html"
                            "../../front/public/robots.txt"
                            "../../front/public/overpass-regular.otf"
                            "../../front/public/overpass.otf"
                            )
register_component()
