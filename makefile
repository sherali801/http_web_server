http_server: http_server.c
	-@gcc http_server.c -o http_server
install: http_server
	-@cp http_server /usr/bin/
	-@chmod a+x /usr/bin/http_server
	-@chmod og-w /usr/bin/http_server
	-@mkdir /var/www
	-@mkdir /var/www/htdocs
	-@mkdir /var/www/logs
	-@mkdir /var/www/responses
	-@sudo cp responses/*.* /var/www/responses/ 
	-@sudo cp http_server.service /etc/systemd/system/
	-@sudo cp htdocs/index.html /var/www/htdocs/  
	-@sudo cp ./man/http_server.3 /usr/share/man/man3/
	@echo "Http Server successfully installed in /usr/bin/"
	@echo "www directory created in /var/"
	@echo "Place your files in /var/www/htdocs/ directory"
	@echo "Logs are placed at /var/www/logs/ directory"
	@echo "Do read the Man(3) page for http_server for information"
uninstall:
	-@rm  /usr/bin/http_server
	-@rm  /etc/systemd/system/http_server.service
	-@rm -rf /var/www
	-@rm /usr/share/man/man3/http_server.3
	@echo "Http Server successfully uninstalled from /usr/bin/"
	@echo "www directory removed from /var/"
clean:
	-@rm http_server
