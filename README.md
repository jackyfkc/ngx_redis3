# ngx_redis3
An Nginx upstream module, used to save the request body(file) to Redis list


Directives
----------

**redis_pass**
 > **syntax:** redis_pass &lt;host&gt;:&lt;port&gt;  
 > **context:** location  

Specify the Redis server backend  
<br>
**redis_db**
 > **syntax:** redis_db &lt;db index&gt;  
 > **context:** location  

Specify db index, should be between 0 and 15  
<br>
**redis_key**  
 > **syntax:** redis_key &lt;name&gt;  
 > **context:** location  

Specify the redis key  
