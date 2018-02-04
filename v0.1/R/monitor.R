A = matrix(c(0,0,0,0,0,0,0),1,7)
B = matrix(c(0,0,0,0,0,0,0),1,7)
delay_time = 5
cost_per_kwh = 0.2284
for (j in 1:10000){
  
  test = readLines("http://192.168.2.16/c")
  X = strsplit(test, ";")
  A[1] = Sys.time()
  for(i in 2:6){
    Z = strsplit(X[[1]][i-1], ":")
    A[i] = as.numeric(Z[[1]][2])
  }
  
  A[7] = 0
  if (j == 1){
    B = A
  }
  else{
    B = rbind(B,A)
    B[j,7] = B[j-1,7]+(B[j,2]/1000)*(delay_time/3600)*cost_per_kwh
  }
  Sys.sleep(delay_time)
  flush.console()
  if (j>1){
    
    par( mfrow = c( 2, 2 ) )  
    
    # FIGURE 1
    par(mar = c(5,5,2,5))  
    plot(as.POSIXct(B[,1], origin = "1970-01-01"), B[,2], xlab = "time",ylab = "Power", type = "n")
    lines(as.POSIXct(B[,1], origin = "1970-01-01"), B[,2])
    
    par(new = T)
    plot(as.POSIXct(B[,1], origin = "1970-01-01"), B[,7], axes = F, xlab = NA, ylab = NA, type = "n")
    lines(as.POSIXct(B[,1], origin = "1970-01-01"), B[,7], col = "red")
    axis (side = 4, col = "red", col.axis = "red")
    mtext("Cost", side = 4, line = 3, col = "red")
    
    r = c (as.POSIXct(min(B[,1]), origin = "1970-01-01"),as.POSIXct(max(B[,1]), origin = "1970-01-01")) 
    axis.POSIXct(1, at = seq(r[1], r[2], by = "hour"))
    #mtext(cat(as.character(B[j,2]), side = 3, line = 0)
    options(scipen=999)
    mtext(test, side = 3, line = 0)
    
    
    #FIGURE 2
    par(mar = c(5,5,2,5))  
    plot(0:10, 0:10,xaxt='n',yaxt='n',bty='n',pch='',ylab='',xlab='')
    text(5,7,paste("Power: ",format(B[j,2], digits=2, nsmall=0, Scientific = FALSE),"W"), col="black", cex=2)
    text(5,3,paste("Cost: $",format(B[j,7], digits=2, nsmall=2, Scientific = FALSE)), col="red", cex=3)
    #
    
    #FIGURE 3
    par(mar = c(5,5,2,5))  
    plot(as.POSIXct(B[,1], origin = "1970-01-01"), B[,3], xlab = "time",ylab = "P1", type = "l", ylim = range(c(B[,3],B[,2])))
    lines(as.POSIXct(B[,1], origin = "1970-01-01"), B[,2], col = "grey")
    
    par(new = T)
    plot(as.POSIXct(B[,1], origin = "1970-01-01"), B[,5], axes = F, xlab = NA, ylab = NA, type = "n")
    lines(as.POSIXct(B[,1], origin = "1970-01-01"), B[,5], col = "blue")
    axis (side = 4, col = "blue", col.axis = "blue")
    mtext("PF1", side = 4, line = 3, col = "blue")
    
    r = c (as.POSIXct(min(B[,1]), origin = "1970-01-01"),as.POSIXct(max(B[,1]), origin = "1970-01-01")) 
    axis.POSIXct(1, at = seq(r[1], r[2], by = "hour"))
    options(scipen=999)
    #
    
    #FIGURE 4
    par(mar = c(5,5,2,5))  
    plot(as.POSIXct(B[,1], origin = "1970-01-01"), B[,4], xlab = "time",ylab = "P2", type = "l", ylim = range(c(B[,3],B[,2])))
    lines(as.POSIXct(B[,1], origin = "1970-01-01"), B[,2], col = "grey")
    
    par(new = T)
    plot(as.POSIXct(B[,1], origin = "1970-01-01"), B[,6], axes = F, xlab = NA, ylab = NA, type = "n")
    lines(as.POSIXct(B[,1], origin = "1970-01-01"), B[,6], col = "blue")
    axis (side = 4, col = "blue", col.axis = "blue")
    mtext("PF2", side = 4, line = 3, col = "blue")
    
    r = c (as.POSIXct(min(B[,1]), origin = "1970-01-01"),as.POSIXct(max(B[,1]), origin = "1970-01-01")) 
    axis.POSIXct(1, at = seq(r[1], r[2], by = "hour"))
    options(scipen=999)
    #
    
    
  }
  
  
}
