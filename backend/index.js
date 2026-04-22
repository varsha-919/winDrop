const express = require("express")

const app = express();

app.get("/live",(req,res)=>
{
    res.send({
        msg:"live",
    })
})

app.listen(4000,()=>
{
    console.log("listenting to port: ",4000);
})