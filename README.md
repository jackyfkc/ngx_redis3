# ngx_redis3
An Nginx upstream module, used to save the request body(file) to Redis list


Directives
----------

**redis_pass**
 > **syntax:** redis_pass <host>:<port>  
 > **context:** location  

Specify the Redis server backend  
  
  
**redis_db**
 > **syntax:** redis_db <db index>  
 > **context:** location  

Specify db index, should be between 0 and 15  
  
  
**redis_key**  
 > **syntax:** redis_key <name>  
 > **context:** location  

Specify the redis key  
